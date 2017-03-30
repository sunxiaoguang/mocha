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
	"net"
	"sync"
	"time"
)

var emptyHeader = make([]PacketHeader, 0)
var emptyOpaqueData = make([]byte, 0)

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

type Channel struct {
	config          *ChannelConfig
	logger          *log.Logger
	conn            io.ReadWriteCloser
	reader          *bufio.Reader
	remoteEndian    binary.ByteOrder
	remoteVersion   int32
	remoteFlags     int32
	packet          chan *Packet
	packetID        int64
	lastReadTime    int64
	remoteID        string
	localID         string
	localAddress    net.Addr
	remoteAddress   net.Addr
	waitGroup       sync.WaitGroup
	shutdownLock    sync.Mutex
	shutdownChannel chan struct{}
	shutdown        bool

	Request  chan *Packet
	Response chan *Packet
}

type ServerChannel struct {
	config       *ChannelConfig
	logger       *log.Logger
	listener     net.Listener
	shutdownLock sync.Mutex
	shutdown     bool
}

func newChannel(conn net.Conn, config *ChannelConfig) (channel *Channel, err error) {
	channel = &Channel{
		config:          config,
		reader:          bufio.NewReader(conn),
		localID:         config.ID,
		conn:            conn,
		Request:         make(chan *Packet, 64),
		Response:        make(chan *Packet, 64),
		packet:          make(chan *Packet, 64),
		shutdownChannel: make(chan struct{}),
		localAddress:    conn.LocalAddr(),
		remoteAddress:   conn.RemoteAddr(),
		logger:          config.Logger,
	}

	channel.waitGroup.Add(1)
	defer channel.waitGroup.Done()
	if err = channel.negotiate(); err != nil {
		channel.doClose()
		return
	}
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

func NewHeaderUnsafe(args ...[]byte) []PacketHeader {
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

func (channel *Channel) SendRequestWithID(id int64, code int32, header []PacketHeader, payload []uint8) {
	channel.packet <- &Packet{
		ID:      id,
		Code:    code,
		tp:      packetTypeRequest,
		flags:   0,
		Header:  header,
		Payload: payload,
	}
	return
}

func (channel *Channel) SendResponse(id int64, code int32, header []PacketHeader, payload []uint8) {
	channel.packet <- &Packet{
		ID:      id,
		Code:    code,
		tp:      packetTypeResponse,
		flags:   0,
		Header:  header,
		Payload: payload,
	}
	return
}

func (channel *Channel) doClose() {
	if channel.shutdown {
		return
	}
	channel.shutdown = true
	close(channel.shutdownChannel)
	channel.conn.Close()
}

func (channel *Channel) Close() {
	channel.shutdownLock.Lock()
	defer channel.shutdownLock.Unlock()
	if channel.shutdown {
		return
	}
	channel.doClose()
	done := make(chan struct{})
	go func() {
		channel.waitGroup.Wait()
		close(done)
	}()
	select {
	case <-done:
	case <-channel.Request:
	case <-channel.Response:
	case <-channel.packet:
	}
	close(channel.packet)
	close(channel.Request)
	close(channel.Response)
	return
}

func (channel *ServerChannel) Accept() (client *Channel, err error) {
	var conn net.Conn
	if conn, err = channel.listener.Accept(); err != nil {
		return
	}
	return newChannel(conn, channel.config)
}

func (channel *ServerChannel) Close() {
	channel.shutdownLock.Lock()
	defer channel.shutdownLock.Unlock()
	if channel.shutdown {
		return
	}
	channel.shutdown = true
	channel.listener.Close()
	return
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
