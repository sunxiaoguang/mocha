package mocha

import (
	"bufio"
	"crypto/rand"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"math"
	"net"
	"sync"
	"sync/atomic"
	"time"
)

var (
	emptyHeader            = make([]PacketHeader, 0)
	emptyOpaqueData        = make([]byte, 0)
	errDuplicatedHeaderKey = errors.New("Duplicated key in packet header")
	ErrChannelClosed       = errors.New("Channel has been closed already")
	ErrWouldBlock          = errors.New("Sending would block because buffer is full")
)

type ChannelFlag uint32

const ChannelFlagNoLinger ChannelFlag = 0x00010000

/*
 Configuration for channel.
 Address: address of server to listen on or connect to
 Timeout: how long a channel remains alive before it's closed since last packet
 Default:
 	Address: localhost:1234
 	Timeout: 30 seconds
	Keepalive: 10 seconds
	Limit: 1MB
	ID: random generated id
	Flags: 0
	Logger: dummy logger which discards everything
*/
type ChannelConfig struct {
	Address               string
	Timeout               time.Duration
	Keepalive             time.Duration
	Limit                 int32
	ID                    string
	Flags                 ChannelFlag
	Logger                *log.Logger
	ChannelBufferCapacity int
}

type lifecycle struct {
	shutdown int32
	barrier  int32
}

func (l *lifecycle) addRef() {
	atomic.AddInt32(&l.barrier, 1)
}

func (l *lifecycle) release() {
	atomic.AddInt32(&l.barrier, -1)
}

func (l *lifecycle) ensure(callable func()) {
	l.addRef()
	defer l.release()
	callable()
}

func (l *lifecycle) safe() bool {
	return atomic.LoadInt32(&l.barrier) == 0
}

func (l *lifecycle) running() bool {
	return atomic.LoadInt32(&l.shutdown) == 0
}

func (l *lifecycle) stopped() bool {
	return !l.running()
}

func (l *lifecycle) stop(prolog, resolve, epilog func()) {
	if atomic.CompareAndSwapInt32(&l.shutdown, 0, 1) {
		if prolog != nil {
			prolog()
		}
		for {
			if l.safe() {
				if epilog != nil {
					epilog()
				}
				break
			} else {
				if resolve != nil {
					resolve()
				}
				time.Sleep(10 * time.Millisecond)
			}
		}
	}
}

type channelStats struct {
	txBytes uint64
	rxBytes uint64

	txPackets   uint64
	txRequests  uint64
	txResponses uint64
	rxPackets   uint64
	rxRequests  uint64
	rxResponses uint64

	lastTxPacketTime   int64
	lastTxRequestTime  int64
	lastTxResponseTime int64
	lastRxPacketTime   int64
	lastRxRequestTime  int64
	lastRxResponseTime int64
}

type ChannelStats struct {
	TxBytes uint64
	RxBytes uint64

	TxPackets   uint64
	TxRequests  uint64
	TxResponses uint64
	RxPackets   uint64
	RxRequests  uint64
	RxResponses uint64

	LastTxPacketTime   time.Time
	LastTxRequestTime  time.Time
	LastTxResponseTime time.Time
	LastRxPacketTime   time.Time
	LastRxRequestTime  time.Time
	LastRxResponseTime time.Time
}

type Channel struct {
	lifecycle
	config        *ChannelConfig
	logger        *log.Logger
	conn          net.Conn
	reader        *bufio.Reader
	remoteEndian  binary.ByteOrder
	remoteVersion int32
	remoteFlags   int32
	packet        chan *Packet
	packetID      int64
	lastReadTime  int64
	lastWriteTime int64
	remoteID      string
	localID       string
	localAddress  net.Addr
	remoteAddress net.Addr
	waitGroup     sync.WaitGroup
	ticker        *time.Ticker
	established   sync.WaitGroup
	linger        bool
	lingerTime    time.Time

	request  chan<- Request
	response chan<- Response

	stats channelStats

	Request  <-chan Request
	Response <-chan Response
	UserData interface{}
}

func convertTsToTime(from int64) time.Time {
	return time.Unix(from/1000000000, from%1000000000)
}

func (c *Channel) Stats() ChannelStats {
	/* use atomic read here to read latest data but do not force update side to
	* flush updates immediately */
	lastTxPacketTime := atomic.LoadInt64(&c.stats.lastTxPacketTime)
	lastTxRequestTime := atomic.LoadInt64(&c.stats.lastTxRequestTime)
	lastTxResponseTime := atomic.LoadInt64(&c.stats.lastTxResponseTime)
	lastRxPacketTime := atomic.LoadInt64(&c.stats.lastRxPacketTime)
	lastRxRequestTime := atomic.LoadInt64(&c.stats.lastRxRequestTime)
	lastRxResponseTime := atomic.LoadInt64(&c.stats.lastRxResponseTime)

	return ChannelStats{
		TxBytes:     atomic.LoadUint64(&c.stats.txBytes),
		TxPackets:   atomic.LoadUint64(&c.stats.txPackets),
		TxRequests:  atomic.LoadUint64(&c.stats.txRequests),
		TxResponses: atomic.LoadUint64(&c.stats.txResponses),
		RxBytes:     atomic.LoadUint64(&c.stats.rxBytes),
		RxPackets:   atomic.LoadUint64(&c.stats.rxPackets),
		RxRequests:  atomic.LoadUint64(&c.stats.rxRequests),
		RxResponses: atomic.LoadUint64(&c.stats.rxResponses),

		LastTxPacketTime:   convertTsToTime(lastTxPacketTime),
		LastTxRequestTime:  convertTsToTime(lastTxRequestTime),
		LastTxResponseTime: convertTsToTime(lastTxResponseTime),
		LastRxPacketTime:   convertTsToTime(lastRxPacketTime),
		LastRxRequestTime:  convertTsToTime(lastRxRequestTime),
		LastRxResponseTime: convertTsToTime(lastRxResponseTime),
	}
}

type ServerChannel struct {
	lifecycle
	config   *ChannelConfig
	logger   *log.Logger
	listener net.Listener
	UserData interface{}
}

func newChannel(conn net.Conn, config *ChannelConfig) (channel *Channel, err error) {
	request := make(chan Request, config.ChannelBufferCapacity)
	response := make(chan Response, config.ChannelBufferCapacity)
	channel = &Channel{
		config:        config,
		reader:        bufio.NewReader(conn),
		localID:       config.ID,
		conn:          conn,
		request:       request,
		Request:       request,
		response:      response,
		Response:      response,
		packet:        make(chan *Packet, config.ChannelBufferCapacity),
		localAddress:  conn.LocalAddr(),
		remoteAddress: conn.RemoteAddr(),
		logger:        config.Logger,
		linger:        config.Flags&ChannelFlagNoLinger == 0,
	}

	channel.established.Add(1)
	go channel.run()
	return channel, nil
}

func (c *Channel) run() {
	c.waitGroup.Add(1)
	defer c.waitGroup.Done()
	err := c.negotiate()
	c.established.Done()
	if err != nil {
		c.doClose()
		return
	}
	sleepTime := c.config.Keepalive
	if atomic.LoadInt32(&c.remoteFlags)&negotiationFlagNoHint != 0 {
		sleepTime = time.Duration(time.Nanosecond * math.MaxInt64)
	}
	if sleepTime > c.config.Timeout {
		sleepTime = c.config.Timeout
	}
	if sleepTime > time.Second {
		/* at least check timeout once a second in case user set a long timeout */
		sleepTime = time.Second
	}
	c.ticker = time.NewTicker(sleepTime)
	c.updateReadTime(&Packet{})
	c.waitGroup.Add(3)
	go c.receive()
	go c.send()
	go c.timer()
	return
}

func (c *Channel) LocalID() string {
	return c.localID
}

func (c *Channel) RemoteID() string {
	return c.remoteID
}

func (c *Channel) LocalAddress() net.Addr {
	return c.localAddress
}

func (c *Channel) RemoteAddress() net.Addr {
	return c.remoteAddress
}

func (c *Channel) SendRequest(code int32, header []PacketHeader, payload []uint8) (id int64, err error) {
	id = c.nextPacketID()
	err = c.SendRequestWithID(id, code, header, payload)
	return
}

func (c *Channel) TrySendRequest(code int32, header []PacketHeader, payload []uint8) (id int64, err error) {
	id = c.nextPacketID()
	err = c.TrySendRequestWithID(id, code, header, payload)
	return
}

func (c *Channel) NextID() int64 {
	return c.nextPacketID()
}

func NewHeaderMust(args ...[]byte) []PacketHeader {
	header, err := NewHeader(args...)
	if err != nil {
		log.Panic(err.Error())
		return nil
	}
	return header
}

func NewHeader(args ...[]byte) (header []PacketHeader, err error) {
	count := len(args)
	if count == 0 {
		header = emptyHeader
		return
	}
	if count%1 != 0 {
		err = errors.New("Arguments not paired up")
		return
	}
	count /= 2
	header = make([]PacketHeader, count, count)
	for idx := 0; idx < count; idx++ {
		header[idx] = PacketHeader{
			Key:   args[2*idx],
			Value: args[(2*idx)+1],
		}
	}
	return
}

func NewHeaderFromMap(args map[string]string) []PacketHeader {
	count := len(args)
	if count == 0 {
		return emptyHeader
	}
	header := make([]PacketHeader, count, count)
	idx := 0
	for k, v := range args {
		header[idx] = PacketHeader{
			Key:   []byte(k),
			Value: []byte(v),
		}
		idx++
	}
	return header
}

func HeaderToMap(header []PacketHeader) (map[string]string, error) {
	m := make(map[string]string, len(header))
	for _, h := range header {
		if _, ok := m[string(h.Key)]; ok {
			return nil, errDuplicatedHeaderKey
		}
	}
	return m, nil
}

func (c *Channel) sendPacket(id int64, code int32, header []PacketHeader, payload []uint8, tp int32, nonblocking bool) (err error) {
	c.ensure(func() {
		if c.running() {
			packet := &Packet{
				ID:      id,
				Code:    code,
				tp:      tp,
				flags:   0,
				Header:  header,
				Payload: payload,
			}

			if nonblocking {
				select {
				case c.packet <- packet:
					if c.linger {
						c.addRef()
					}
				default:
					err = ErrWouldBlock
				}
			} else {
				c.packet <- packet
				if c.linger {
					c.addRef()
				}
			}
		} else {
			err = ErrChannelClosed
		}
	})
	return
}

func (c *Channel) TrySendRequestWithID(id int64, code int32, header []PacketHeader, payload []uint8) error {
	return c.sendPacket(id, code, header, payload, packetTypeRequest, true)
}

func (c *Channel) SendRequestWithID(id int64, code int32, header []PacketHeader, payload []uint8) error {
	return c.sendPacket(id, code, header, payload, packetTypeRequest, false)
}

func (c *Channel) SendResponse(id int64, code int32, header []PacketHeader, payload []uint8) error {
	return c.sendPacket(id, code, header, payload, packetTypeResponse, false)
}

func (c *Channel) TrySendResponse(id int64, code int32, header []PacketHeader, payload []uint8) error {
	return c.sendPacket(id, code, header, payload, packetTypeResponse, true)
}

func (c *Channel) resolveLingerTimeout() {
	if time.Now().Sub(c.lingerTime) > c.config.Timeout {
		c.conn.Close()
	}
}

func (c *Channel) recordLingerTime() {
	c.lingerTime = time.Now()
}

func (c *Channel) doClose() {
	c.stop(nil, c.resolveLingerTimeout, func() {
		c.conn.Close()
		close(c.packet)
		close(c.request)
		close(c.response)
	})
}

func (c *Channel) Close() {
	c.NonBlockingClose()
	c.Join()
}

func (c *Channel) Join() {
	c.waitGroup.Wait()
}

func (c *Channel) NonBlockingClose() {
	go c.doClose()
}

func (c *ServerChannel) Accept() (client *Channel, err error) {
	c.ensure(func() {
		if c.running() {
			var conn net.Conn
			if conn, err = c.listener.Accept(); err == nil {
				client, err = newChannel(conn, c.config)
			}
		} else {
			err = ErrChannelClosed
		}
	})
	if client == nil && err == nil {
		err = ErrChannelClosed
	}
	return
}

func (c *ServerChannel) Close() {
	c.stop(nil, func() {
		c.listener.Close()
	}, nil)
}

func checkConfig(config *ChannelConfig) *ChannelConfig {
	result := &ChannelConfig{}
	if config != nil {
		*result = *config
	}
	if len(result.Address) == 0 {
		result.Address = "localhost:1234"
	}
	if result.Timeout == 0 {
		result.Timeout = time.Duration(30 * time.Second)
	} else if result.Timeout < time.Millisecond*10 {
		result.Timeout = time.Millisecond * 10
	}
	if result.Keepalive == 0 {
		result.Keepalive = time.Duration(10 * time.Second)
	} else if result.Keepalive < time.Second {
		result.Keepalive = time.Second
	}
	if result.Limit <= 0 {
		result.Limit = 1024 * 1024
	}
	if result.Flags != 0 {
		/* only accept public flags */
		result.Flags = result.Flags & 0xFFFF0000
	}
	if result.Logger == nil {
		result.Logger = log.New(ioutil.Discard, "MochaRPC", 0)
	}
	if result.ChannelBufferCapacity < 64 {
		result.ChannelBufferCapacity = 64
	}
	if len(result.ID) == 0 {
		uuid := make([]byte, 16)
		n, err := io.ReadFull(rand.Reader, uuid)
		if n == len(uuid) && err == nil {
			uuid[8] = uuid[8]&^0xc0 | 0x80
			uuid[6] = uuid[6]&^0xf0 | 0x40
			result.ID = fmt.Sprintf("%x-%x-%x-%x-%x", uuid[0:4], uuid[4:6], uuid[6:8], uuid[8:10], uuid[10:])
		}
	}
	return result
}

func BindListener(listener net.Listener, config *ChannelConfig) (channel *ServerChannel, err error) {
	config = checkConfig(config)
	config.Address = listener.Addr().String()
	channel = &ServerChannel{
		config:   config,
		logger:   config.Logger,
		listener: listener,
	}
	return
}

func Bind(config *ChannelConfig) (channel *ServerChannel, err error) {
	config = checkConfig(config)
	var listener net.Listener
	if listener, err = net.Listen("tcp", config.Address); err != nil {
		return
	}
	return BindListener(listener, config)
}

func ConnectConn(conn net.Conn, config *ChannelConfig) (channel *Channel, err error) {
	config = checkConfig(config)
	config.Address = conn.LocalAddr().String()
	c, err := newChannel(conn, config)
	if err == nil {
		c.established.Wait()
	}
	return c, err
}

func Connect(config *ChannelConfig) (channel *Channel, err error) {
	config = checkConfig(config)
	var conn net.Conn
	if conn, err = net.DialTimeout("tcp", config.Address, config.Timeout); err != nil {
		return
	}
	return ConnectConn(conn, config)

}
