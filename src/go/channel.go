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

var emptyHeader = make([]PacketHeader, 0)
var emptyOpaqueData = make([]byte, 0)
var errChannelClosed = errors.New("Channel has been closed already")
var errDuplicatedHeaderKey = errors.New("Duplicated key in packet header")

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
	Address   string
	Timeout   time.Duration
	Keepalive time.Duration
	Limit     int32
	ID        string
	Flags     int32
	Logger    *log.Logger
}

type lifecycle struct {
	shutdown int32
	barrier  int32
}

func (l *lifecycle) ensure(callable func()) {
	atomic.AddInt32(&l.barrier, 1)
	defer atomic.AddInt32(&l.barrier, -1)
	if l.running() {
		callable()
	}
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
	remoteID      string
	localID       string
	localAddress  net.Addr
	remoteAddress net.Addr
	waitGroup     sync.WaitGroup
	ticker        *time.Ticker
	established   sync.WaitGroup

	request  chan<- Request
	response chan<- Response

	Request  <-chan Request
	Response <-chan Response
}

type ServerChannel struct {
	lifecycle
	config   *ChannelConfig
	logger   *log.Logger
	listener net.Listener
}

func newChannel(conn net.Conn, config *ChannelConfig) (channel *Channel, err error) {
	request := make(chan Request, 64)
	response := make(chan Response, 64)
	channel = &Channel{
		config:        config,
		reader:        bufio.NewReader(conn),
		localID:       config.ID,
		conn:          conn,
		request:       request,
		Request:       request,
		response:      response,
		Response:      response,
		packet:        make(chan *Packet, 64),
		localAddress:  conn.LocalAddr(),
		remoteAddress: conn.RemoteAddr(),
		logger:        config.Logger,
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
	c.updateReadTime()
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

func (c *Channel) sendPacket(id int64, code int32, header []PacketHeader, payload []uint8, tp int32) (err error) {
	c.ensure(func() {
		if c.running() {
			c.packet <- &Packet{
				ID:      id,
				Code:    code,
				tp:      tp,
				flags:   0,
				Header:  header,
				Payload: payload,
			}
		} else {
			err = errChannelClosed
		}
	})
	return
}

func (c *Channel) SendRequestWithID(id int64, code int32, header []PacketHeader, payload []uint8) error {
	return c.sendPacket(id, code, header, payload, packetTypeRequest)
}

func (c *Channel) SendResponse(id int64, code int32, header []PacketHeader, payload []uint8) error {
	return c.sendPacket(id, code, header, payload, packetTypeResponse)
}

func drainRequests(c <-chan Request) {
	for {
		select {
		case <-c:
		default:
			return
		}
	}
}

func drainResponses(c <-chan Response) {
	for {
		select {
		case <-c:
		default:
			return
		}
	}
}

func drainPackets(c <-chan *Packet) {
	for {
		select {
		case <-c:
		default:
			return
		}
	}
}

func (c *Channel) doClose() {
	c.stop(func() {
		c.conn.Close()
	}, func() {
		drainPackets(c.packet)
		drainRequests(c.Request)
		drainResponses(c.Response)
	}, func() {
		close(c.packet)
		close(c.request)
		close(c.response)
	})
}

func (c *Channel) Close() {
	c.doClose()
	c.waitGroup.Wait()
}

func (c *ServerChannel) Accept() (client *Channel, err error) {
	c.ensure(func() {
		var conn net.Conn
		if conn, err = c.listener.Accept(); err == nil {
			client, err = newChannel(conn, c.config)
		}
	})
	if client == nil && err == nil {
		err = errChannelClosed
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
	if result.Timeout.Nanoseconds() == 0 {
		result.Timeout = time.Duration(30 * time.Second)
	}
	if result.Keepalive.Nanoseconds() == 0 {
		result.Keepalive = time.Duration(10 * time.Second)
	}
	if result.Limit == 0 {
		result.Limit = 1024 * 1024
	}
	if result.Flags != 0 {
		result.Flags = 0
	}
	if result.Logger == nil {
		result.Logger = log.New(ioutil.Discard, "MochaRPC", 0)
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
