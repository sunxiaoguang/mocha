package mocha

import (
	"bytes"
	"compress/zlib"
	"container/list"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"log"
	"math"
	"strings"
	"sync"
	"sync/atomic"
	"time"
	"unsafe"
)

const (
	negotiationHeaderMagicSize        = 4
	negotiationHeaderFlagsSize        = 4
	negotiationHeaderPeerIDLengthSize = 4
	negotiationHeaderFixedSize        = negotiationHeaderMagicSize + negotiationHeaderFlagsSize + negotiationHeaderPeerIDLengthSize

	packetHeaderIDSize      = 8
	packetHeaderCodeSize    = 4
	packetHeaderFlagsSize   = 4
	packetHeaderHeaderSize  = 4
	packetHeaderPayloadSize = 4
	packetHeaderFixedSize   = packetHeaderIDSize + packetHeaderCodeSize + packetHeaderFlagsSize + packetHeaderHeaderSize + packetHeaderPayloadSize

	packetFlagHeaderZlibEncoded = 1

	packetTypeRequest  = 0
	packetTypeResponse = 1
	packetTypeHint     = 2

	HintTypeKeepalive = 0

	negotiationFlagAcceptZlib = 1
	negotiationFlagNoHint     = 2
	negotiationFlagMask       = 0XFFFF
)

var nativeEndian = getNativeEndian()
var foreignEndian = getForeignEndian()

func isNativeLittleEndian() bool {
	var tmp int = 0x1
	bytes := (*[int(unsafe.Sizeof(0))]byte)(unsafe.Pointer(&tmp))
	if bytes[0] == 0 {
		return false
	}
	return true
}

func getForeignEndian() binary.ByteOrder {
	if isNativeLittleEndian() {
		return binary.BigEndian
	}
	return binary.LittleEndian
}

func getNativeEndian() binary.ByteOrder {
	if isNativeLittleEndian() {
		return binary.LittleEndian
	}
	return binary.BigEndian
}

type negotiationHeader []byte

func (header negotiationHeader) magicCode() (order binary.ByteOrder, err error) {
	code := nativeEndian.Uint32(header[0:4])
	if code == 0x4D4F4341 {
		order = nativeEndian
	} else if code == 0x41434F4D {
		order = foreignEndian
	} else {
		err = errors.New("Incompatible Protocol")
	}
	return
}

func (header negotiationHeader) flags(endian binary.ByteOrder) (version int32, flags int32) {
	flags = int32(endian.Uint32(header[4:8]))
	version = flags & 0xFF
	flags >>= 8
	return
}

func (header negotiationHeader) peerIDSize(endian binary.ByteOrder) int32 {
	return int32(endian.Uint32(header[8:12]))
}

type packetHeader []byte

func (header packetHeader) id(endian binary.ByteOrder) int64 {
	return int64(endian.Uint64(header[0:8]))
}

func (header packetHeader) code(endian binary.ByteOrder) int32 {
	return int32(endian.Uint32(header[8:12]))
}

func (header packetHeader) flags(endian binary.ByteOrder) (tp int32, flags int32) {
	flags = int32(endian.Uint32(header[12:16]))
	tp = flags & 0xFF
	flags >>= 8
	return
}

func (header packetHeader) headerSize(endian binary.ByteOrder) int32 {
	return int32(endian.Uint32(header[16:20]))
}

func (header packetHeader) payloadSize(endian binary.ByteOrder) int32 {
	return int32(endian.Uint32(header[20:24]))
}

type PacketHeader struct {
	Key   []byte
	Value []byte
}

type Packet struct {
	ID      int64
	Code    int32
	tp      int32
	Header  []PacketHeader
	Payload []byte
	flags   int32
	size    int32
}

type Response *Packet
type Request *Packet

func (p *Packet) isResponse() bool {
	return p.tp == packetTypeResponse
}

func (p *Packet) IsResponse() Response {
	if p.isResponse() {
		return p
	} else {
		return nil
	}
}

func (p *Packet) isRequest() bool {
	return p.tp == packetTypeRequest
}

func (p *Packet) IsRequest() Request {
	if p.isRequest() {
		return p
	} else {
		return nil
	}
}

func readPacketHeaderData(data []byte, endian binary.ByteOrder, logger *log.Logger) (header []PacketHeader, err error) {
	var tmp []byte
	limit := len(data)
	lst := list.New()
	var count int
	for offset := 0; offset < len(data); {
		reader := bytes.NewReader(data[offset:limit])
		if tmp, err = readStringBytes(endian, reader, logger); err != nil {
			return
		}
		key := tmp
		offset = limit - reader.Len()
		reader = bytes.NewReader(data[offset:limit])
		if tmp, err = readStringBytes(endian, reader, logger); err != nil {
			return
		}
		lst.PushBack(PacketHeader{
			Key:   key,
			Value: tmp,
		})
		offset = limit - reader.Len()
		count++
	}
	header = make([]PacketHeader, count)
	count = 0
	for e := lst.Front(); e != nil; e = e.Next() {
		header[count] = (e.Value).(PacketHeader)
		count++
	}
	return
}

func readPlainPacketHeader(size int32, endian binary.ByteOrder, reader io.Reader, logger *log.Logger) (header []PacketHeader, err error) {
	var data []byte
	if data, err = readOpaqueData(size, reader, logger); err != nil {
		return
	}
	return readPacketHeaderData(data, endian, logger)
}

func readZlibCompressedPacketHeader(size int32, limit int32, endian binary.ByteOrder, reader io.Reader, logger *log.Logger) (header []PacketHeader, err error) {
	var data []byte
	if data, err = readOpaqueData(size, reader, logger); err != nil {
		return
	}
	if size < 5 {
		err = errors.New("Compressed packet header is corrupted")
		return
	}

	if size, err = readInt32(endian, bytes.NewReader(data[0:4]), logger); err != nil {
		return
	}

	if size > limit {
		err = fmt.Errorf("Decompressed packet header size %d is greater than limit of %d", size, limit)
		return
	}

	if reader, err = zlib.NewReader(bytes.NewReader(data[4:len(data)])); err != nil {
		return
	}
	data = make([]byte, size)
	if _, err = io.ReadFull(reader, data); err != nil {
		return
	}
	return readPacketHeaderData(data, endian, logger)
}

func readPacketHeader(flags int32, size int32, limit int32, endian binary.ByteOrder, reader io.Reader, logger *log.Logger) (header []PacketHeader, err error) {
	if size == 0 {
		return emptyHeader, nil
	}
	if flags&packetFlagHeaderZlibEncoded != 0 {
		return readZlibCompressedPacketHeader(size, limit, endian, reader, logger)
	}
	return readPlainPacketHeader(size, endian, reader, logger)
}

func readPacket(limit int32, endian binary.ByteOrder, reader io.Reader, logger *log.Logger) (packet *Packet, err error) {
	packetHeader := packetHeader(make([]byte, packetHeaderFixedSize))
	if _, err = io.ReadFull(reader, packetHeader); err != nil {
		if err != io.EOF && !strings.Contains(err.Error(), "closed") && !strings.Contains(err.Error(), "reset by peer") {
			logger.Printf("[E]: Failed to read packet header %v", err)
		}
		return
	}
	packet = &Packet{
		ID:   packetHeader.id(endian),
		Code: packetHeader.code(endian),
	}

	packet.tp, packet.flags = packetHeader.flags(endian)

	headerSize := packetHeader.headerSize(endian)
	if headerSize > limit {
		err = fmt.Errorf("Packet header size %d is greater than limit of %d", headerSize, limit)
	}
	payloadSize := packetHeader.payloadSize(endian)
	if payloadSize > limit {
		err = fmt.Errorf("Packet payload size %d is greater than limit of %d", payloadSize, limit)
	}
	if packet.Header, err = readPacketHeader(packet.flags, headerSize, limit, endian, reader, logger); err != nil {
		return
	}
	packet.Payload, err = readOpaqueData(payloadSize, reader, logger)
	packet.size = packetHeaderFixedSize + headerSize + payloadSize
	return
}

func readInt32(endian binary.ByteOrder, reader io.Reader, logger *log.Logger) (value int32, err error) {
	buffer := make([]byte, 4)
	if _, err = io.ReadFull(reader, buffer); err != nil {
		if err != io.EOF && !strings.Contains(err.Error(), "closed") && !strings.Contains(err.Error(), "reset by peer") {
			logger.Printf("[E]: Failed to read int32 %v", err)
		}
		return
	}
	value = int32(endian.Uint32(buffer))
	return
}

func readOpaqueData(size int32, reader io.Reader, logger *log.Logger) (data []byte, err error) {
	if size == 0 {
		data = emptyOpaqueData
		return
	}
	data = make([]byte, size)
	if _, err = io.ReadFull(reader, data); err != nil {
		if err != io.EOF && !strings.Contains(err.Error(), "closed") && !strings.Contains(err.Error(), "reset by peer") {
			logger.Printf("[E]: Failed to read opaque data%v", err)
		}
		return
	}
	return
}

func readStringBytes(endian binary.ByteOrder, reader io.Reader, logger *log.Logger) (bytes []byte, err error) {
	var size int32
	if size, err = readInt32(endian, reader, logger); err != nil {
		return
	}
	return readStringBodyBytes(size, reader, logger)
}

func readStringBodyBytes(size int32, reader io.Reader, logger *log.Logger) (bytes []byte, err error) {
	if bytes, err = readOpaqueData(size+1, reader, logger); err != nil {
		return
	}
	bytes = bytes[0:size]
	return
}

func readStringBody(size int32, reader io.Reader, logger *log.Logger) (str string, err error) {
	if bytes, err2 := readStringBodyBytes(size, reader, logger); err2 != nil {
		err = err2
	} else {
		str = string(bytes)
	}
	return
}

func serializePacketHeader(packet *Packet) []byte {
	lenBuffer := make([]byte, 4)
	var buffer bytes.Buffer
	if packet.Header == nil || len(packet.Header) == 0 {
		return nil
	}
	for _, header := range packet.Header {
		key := []byte(header.Key)
		nativeEndian.PutUint32(lenBuffer, uint32(len(key)))
		buffer.Write(lenBuffer)
		buffer.Write(key)
		buffer.WriteByte(0)
		value := []byte(header.Value)
		nativeEndian.PutUint32(lenBuffer, uint32(len(value)))
		buffer.Write(lenBuffer)
		buffer.Write(value)
		buffer.WriteByte(0)
	}
	return buffer.Bytes()
}

func (c *Channel) doSendPacket(packet *Packet) (err error) {
	header := serializePacketHeader(packet)
	headerLength := len(header)
	payloadLength := 0
	if packet.Payload != nil {
		payloadLength = len(packet.Payload)
	}
	var length = packetHeaderFixedSize + headerLength + payloadLength
	buffer := make([]byte, length)
	nativeEndian.PutUint64(buffer[0:8], uint64(packet.ID))
	nativeEndian.PutUint32(buffer[8:12], uint32(packet.Code))
	nativeEndian.PutUint32(buffer[12:16], uint32(packet.flags<<8|packet.tp))
	nativeEndian.PutUint32(buffer[16:20], uint32(headerLength))
	nativeEndian.PutUint32(buffer[20:24], uint32(payloadLength))
	if headerLength > 0 {
		copy(buffer[24:length], header)
	}
	if payloadLength > 0 {
		copy(buffer[24+headerLength:length], packet.Payload)
	}
	packet.size = int32(length)
	c.updateWriteTime(packet)
	_, err = c.conn.Write(buffer)
	return
}

func (c *Channel) updateWriteTime(packet *Packet) {
	now := time.Now().UnixNano()
	c.stats.txBytes += uint64(packet.size)
	c.stats.txPackets++
	c.stats.lastTxPacketTime = now
	switch packet.tp {
	case packetTypeRequest:
		c.stats.txRequests++
		c.stats.lastTxRequestTime = now
	case packetTypeResponse:
		c.stats.txResponses++
		c.stats.lastTxResponseTime = now
	}
	atomic.StoreInt64(&c.lastWriteTime, now)
}

func (c *Channel) updateReadTime(packet *Packet) {
	now := time.Now().UnixNano()
	c.stats.rxBytes += uint64(packet.size)
	c.stats.rxPackets++
	c.stats.lastRxPacketTime = now
	switch packet.tp {
	case packetTypeRequest:
		c.stats.rxRequests++
		c.stats.lastRxRequestTime = now
	case packetTypeResponse:
		c.stats.rxResponses++
		c.stats.lastRxResponseTime = now
	}
	atomic.StoreInt64(&c.lastReadTime, now)
}

func (c *Channel) onKeepalive() {
}

func (c *Channel) onHint(packet *Packet) {
	switch packet.Code {
	case HintTypeKeepalive:
		c.onKeepalive()
	}
}

func (c *Channel) onRequest(request Request) {
	c.ensure(func() {
		c.request <- request
	})
}

func (c *Channel) onResponse(response Response) {
	c.ensure(func() {
		c.response <- response
	})
}

func (c *Channel) negotiate() (err error) {
	var n int
	localID := []byte(c.localID)
	n = negotiationHeaderFixedSize + len(localID) + 1
	buffer := make([]byte, n)
	nativeEndian.PutUint32(buffer, uint32(0X4D4F4341))
	nativeEndian.PutUint32(buffer[4:8], uint32(0))
	nativeEndian.PutUint32(buffer[8:12], uint32(len(localID)))
	copy(buffer[12:n], localID)
	if n, err = c.conn.Write(buffer); n != len(buffer) || err != nil {
		c.logger.Print("[E] Failed to write negotiation header")
		return
	}

	negotiationHeader := negotiationHeader(buffer[0:negotiationHeaderFixedSize])
	c.conn.SetReadDeadline(time.Now().Add(c.config.Timeout))
	if _, err = io.ReadFull(c.reader, negotiationHeader); err != nil {
		if err != io.EOF && !strings.Contains(err.Error(), "closed") && !strings.Contains(err.Error(), "reset by peer") {
			c.config.Logger.Printf("[E]: Failed to read negotiation header %v", err)
		}
		return
	}
	if c.remoteEndian, err = negotiationHeader.magicCode(); err != nil {
		c.config.Logger.Printf("[E]: %v", err)
		return
	}
	c.remoteVersion, c.remoteFlags = negotiationHeader.flags(c.remoteEndian)
	peerIDSize := negotiationHeader.peerIDSize(c.remoteEndian)
	if peerIDSize > c.config.Limit {
		c.config.Logger.Printf("[E]: peer id size %d is greater than limit of %d", peerIDSize, c.config.Limit)
		err = errors.New("Peer id too long")
		return
	}
	c.remoteID, err = readStringBody(peerIDSize, c.reader, c.logger)
	c.conn.SetReadDeadline(time.Now().Add(time.Hour * 0xffff))
	mtx := sync.Mutex{}
	/*
	 * issue a full memory barrier so later r/w to these in dedicated goroutine
	 * doesn't require synchronization
	 */
	mtx.Lock()
	c.stats.txBytes = uint64(negotiationHeaderFixedSize + len(localID))
	c.stats.rxBytes = uint64(negotiationHeaderFixedSize + peerIDSize)
	mtx.Unlock()
	return
}

func (c *Channel) receive() {
	defer c.waitGroup.Done()
	for {
		/* read packets */
		var packet *Packet
		var err error
		if packet, err = readPacket(c.config.Limit, c.remoteEndian, c.reader, c.config.Logger); err != nil {
			c.NonBlockingClose()
			return
		}
		c.updateReadTime(packet)
		switch packet.tp {
		case packetTypeHint:
			c.onHint(packet)
		case packetTypeRequest:
			c.onRequest(packet)
		case packetTypeResponse:
			c.onResponse(packet)
		}
	}
}

func (c *Channel) send() {
	defer c.waitGroup.Done()
	if c.linger {
		for packet := range c.packet {
			if err := c.doSendPacket(packet); err != nil {
				c.NonBlockingClose()
			}
			c.release()
		}
	} else {
		for packet := range c.packet {
			if c.doSendPacket(packet) != nil {
				c.NonBlockingClose()
			}
		}
	}
}

func (c *Channel) nextPacketID() int64 {
	return atomic.AddInt64(&c.packetID, 1)
}

func (c *Channel) sendKeepalive() {
	c.sendPacket(c.nextPacketID(), HintTypeKeepalive, nil, nil, packetTypeHint, true)
}

func (c *Channel) checkTimeout(timeout time.Duration, now time.Time) {
	readTime := atomic.LoadInt64(&c.lastReadTime)
	if readTime+timeout.Nanoseconds() < now.UnixNano() {
		c.logger.Printf("[E] Hasn't read any packet from remote peer %v within %v, close it",
			c.RemoteAddress(), timeout)
		c.NonBlockingClose()
	}
}

func (c *Channel) timer() {
	defer c.waitGroup.Done()
	defer c.ticker.Stop()
	keepAlive := c.config.Keepalive
	if atomic.LoadInt32(&c.remoteFlags)&negotiationFlagNoHint != 0 {
		keepAlive = time.Duration(time.Nanosecond * math.MaxInt64)
	}
	timeout := c.config.Timeout
	now := time.Now()
	nextKeepaliveTime := now.Add(keepAlive)
	nextTimeoutTime := now.Add(timeout)
	for now = range c.ticker.C {
		if c.stopped() {
			break
		}
		if now.After(nextKeepaliveTime) {
			c.sendKeepalive()
			nextKeepaliveTime = now.Add(keepAlive)
		}
		if now.After(nextTimeoutTime) {
			c.checkTimeout(timeout, now)
			nextTimeoutTime = now.Add(timeout)
		}
	}
}
