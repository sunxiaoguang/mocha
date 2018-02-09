package mocharpc

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
var alreadyStoppedError = errors.New("Server channel has been stopped already")

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

	Request  chan *Packet
	Response chan *Packet
}

type ServerChannel struct {
	lifecycle
	config   *ChannelConfig
	logger   *log.Logger
	listener net.Listener
}

func newChannel(conn net.Conn, config *ChannelConfig) (channel *Channel, err error) {
	channel = &Channel{
		config:        config,
		reader:        bufio.NewReader(conn),
		localID:       config.ID,
		conn:          conn,
		Request:       make(chan *Packet, 64),
		Response:      make(chan *Packet, 64),
		packet:        make(chan *Packet, 64),
		localAddress:  conn.LocalAddr(),
		remoteAddress: conn.RemoteAddr(),
		logger:        config.Logger,
	}

	go channel.run()
	return channel, nil
}

func (channel *Channel) run() {
	channel.waitGroup.Add(1)
	defer channel.waitGroup.Done()
	if err := channel.negotiate(); err != nil {
		channel.doClose()
		return
	}
	sleepTime := channel.config.Keepalive
	if atomic.LoadInt32(&channel.remoteFlags)&negotiationFlagNoHint != 0 {
		sleepTime = time.Duration(time.Nanosecond * math.MaxInt64)
	}
	if sleepTime > channel.config.Timeout {
		sleepTime = channel.config.Timeout
	}
	if sleepTime > time.Second {
		/* at least check timeout once a second in case user set a long timeout */
		sleepTime = time.Second
	}
	channel.ticker = time.NewTicker(sleepTime)
	channel.updateReadTime()
	channel.waitGroup.Add(3)
	go channel.receive()
	go channel.send()
	go channel.timer()
	return
}

func (channel *Channel) LocalID() string {
	return channel.localID
}

func (channel *Channel) RemoteID() string {
	return channel.remoteID
}

func (channel *Channel) LocalAddress() net.Addr {
	return channel.localAddress
}

func (channel *Channel) RemoteAddress() net.Addr {
	return channel.remoteAddress
}

func (channel *Channel) SendRequest(code int32, header []PacketHeader, payload []uint8) (id int64) {
	id = channel.nextPacketID()
	channel.SendRequestWithID(id, code, header, payload)
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

func (channel *Channel) sendPacket(id int64, code int32, header []PacketHeader, payload []uint8, tp int32) {
	channel.ensure(func() {
		if channel.running() {
			channel.packet <- &Packet{
				ID:      id,
				Code:    code,
				tp:      tp,
				flags:   0,
				Header:  header,
				Payload: payload,
			}
		}
	})
}

func (channel *Channel) SendRequestWithID(id int64, code int32, header []PacketHeader, payload []uint8) {
	channel.sendPacket(id, code, header, payload, packetTypeRequest)
}

func (channel *Channel) SendResponse(id int64, code int32, header []PacketHeader, payload []uint8) {
	channel.sendPacket(id, code, header, payload, packetTypeResponse)
}

func drainChannel(channel chan *Packet) {
	for {
		select {
		case <-channel:
		default:
			return
		}
	}
}

func (channel *Channel) doClose() {
	channel.stop(func() {
		channel.conn.Close()
	}, func() {
		drainChannel(channel.packet)
		drainChannel(channel.Request)
		drainChannel(channel.Response)
	}, func() {
		close(channel.packet)
		close(channel.Request)
		close(channel.Response)
	})
}

func (channel *Channel) Close() {
	channel.doClose()
	channel.waitGroup.Wait()
}

func (channel *ServerChannel) Accept() (client *Channel, err error) {
	channel.ensure(func() {
		var conn net.Conn
		if conn, err = channel.listener.Accept(); err == nil {
			client, err = newChannel(conn, channel.config)
		}
	})
	if client == nil && err == nil {
		err = alreadyStoppedError
	}
	return
}

func (channel *ServerChannel) Close() {
	channel.stop(nil, func() {
		channel.listener.Close()
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
	return newChannel(conn, config)
}

func Connect(config *ChannelConfig) (channel *Channel, err error) {
	config = checkConfig(config)
	var conn net.Conn
	if conn, err = net.Dial("tcp", config.Address); err != nil {
		return
	}
	return newChannel(conn, config)
}
