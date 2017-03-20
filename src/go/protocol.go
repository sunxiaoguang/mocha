package mocharpc

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
	"runtime"
	"strings"
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
	Key   string
	Value string
}

type Packet struct {
	ID      int64
	Code    int32
	Header  []PacketHeader
	Payload []byte
	tp      int32
	flags   int32
}

func readPacketHeaderData(data []byte, endian binary.ByteOrder, logger *log.Logger) (header []PacketHeader, err error) {
	var str string
	limit := len(data)
	lst := list.New()
	var count int
	for offset := 0; offset < len(data); {
		reader := bytes.NewReader(data[offset:limit])
		if str, err = readString(endian, reader, logger); err != nil {
			return
		}
		key := str
		offset = limit - reader.Len()
		reader = bytes.NewReader(data[offset:limit])
		if str, err = readString(endian, reader, logger); err != nil {
			return
		}
		lst.PushBack(PacketHeader{
			Key:   key,
			Value: str,
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

func readString(endian binary.ByteOrder, reader io.Reader, logger *log.Logger) (str string, err error) {
	var size int32
	if size, err = readInt32(endian, reader, logger); err != nil {
		return
	}
	return readStringBody(size, reader, logger)
}

func readStringBody(size int32, reader io.Reader, logger *log.Logger) (str string, err error) {
	var body []byte
	if body, err = readOpaqueData(size+1, reader, logger); err != nil {
		return
	}
	str = string(body)
	return
}

func (channel *Channel) serializePacketHeader(packet *Packet) []byte {
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

func (channel *Channel) sendPacket(packet *Packet) (err error) {
	header := channel.serializePacketHeader(packet)
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
	_, err = channel.conn.Write(buffer)
	return
}

func (channel *Channel) updateReadTime() {
	atomic.StoreInt64(&channel.lastReadTime, time.Now().UnixNano())
}

func (channel *Channel) onKeepalive() {
}

func (channel *Channel) onHint(packet *Packet) {
	switch packet.Code {
	case HintTypeKeepalive:
		channel.onKeepalive()
	}
}

func (channel *Channel) onRequest(packet *Packet) {
	channel.Request <- packet
}

func (channel *Channel) onResponse(packet *Packet) {
	channel.Response <- packet
}

func (channel *Channel) negotiate() (err error) {
	var n int
	localID := []byte(channel.localID)
	n = negotiationHeaderFixedSize + len(localID) + 1
	buffer := make([]byte, n)
	nativeEndian.PutUint32(buffer, uint32(0X4D4F4341))
	nativeEndian.PutUint32(buffer[4:8], uint32(0))
	nativeEndian.PutUint32(buffer[8:12], uint32(len(localID)))
	copy(buffer[12:n], localID)
	if n, err = channel.conn.Write(buffer); n != len(buffer) || err != nil {
		channel.logger.Print("[E] Failed to write negotiation header")
		return
	}

	negotiationHeader := negotiationHeader(buffer[0:negotiationHeaderFixedSize])
	if _, err = io.ReadFull(channel.reader, negotiationHeader); err != nil {
		if err != io.EOF && !strings.Contains(err.Error(), "closed") && !strings.Contains(err.Error(), "reset by peer") {
			channel.config.Logger.Printf("[E]: Failed to read negotiation header %v", err)
		}
		return
	}
	if channel.remoteEndian, err = negotiationHeader.magicCode(); err != nil {
		channel.config.Logger.Printf("[E]: %v", err)
		return
	}
	channel.remoteVersion, channel.remoteFlags = negotiationHeader.flags(channel.remoteEndian)
	peerIDSize := negotiationHeader.peerIDSize(channel.remoteEndian)
	if peerIDSize > channel.config.Limit {
		channel.config.Logger.Printf("[E]: peer id size %d is greater than limit of %d", peerIDSize, channel.config.Limit)
		err = errors.New("Peer id too long")
		return
	}
	channel.remoteID, err = readStringBody(peerIDSize, channel.reader, channel.logger)
	return
}

func (channel *Channel) receive() {
	defer channel.waitGroup.Done()
	for {
		/* read packets */
		var packet *Packet
		var err error
		if packet, err = readPacket(channel.config.Limit, channel.remoteEndian, channel.reader, channel.config.Logger); err != nil {
			channel.doClose()
			return
		}
		channel.updateReadTime()
		switch packet.tp {
		case packetTypeHint:
			channel.onHint(packet)
		case packetTypeRequest:
			channel.onRequest(packet)
		case packetTypeResponse:
			channel.onResponse(packet)
		}
	}
}

func (channel *Channel) send() {
	defer channel.waitGroup.Done()
	for {
		select {
		case packet, running := <-channel.packet:
			if !running || channel.sendPacket(packet) != nil {
				channel.doClose()
			}
		case <-channel.shutdownChannel:
			return
		}
	}
}

func (channel *Channel) nextPacketID() int64 {
	return atomic.AddInt64(&channel.packetID, 1)
}

func (channel *Channel) sendKeepalive() {
	channel.packet <- &Packet{
		ID:    channel.nextPacketID(),
		Code:  HintTypeKeepalive,
		tp:    packetTypeHint,
		flags: 0,
	}
}

func (channel *Channel) checkTimeout(timeout time.Duration, now time.Time) {
	readTime := atomic.LoadInt64(&channel.lastReadTime)
	if readTime+timeout.Nanoseconds() < now.UnixNano() {
		channel.logger.Printf("[E] Hasn't read any packet from remote peer %v within %v, close it",
			channel.RemoteAddress(), timeout)
		channel.doClose()
	}
}

func (channel *Channel) timer() {
	defer channel.waitGroup.Done()
	keepAlive := channel.config.Keepalive
	if atomic.LoadInt32(&channel.remoteFlags)&negotiationFlagNoHint != 0 {
		keepAlive = time.Duration(time.Nanosecond * math.MaxInt64)
	}
	timeout := channel.config.Timeout
	sleepTime := keepAlive
	if sleepTime > timeout {
		sleepTime = timeout
	}

	nextKeepaliveTime := time.Now().Add(keepAlive)
	nextTimeoutTime := time.Now().Add(timeout)

	for {
		select {
		case <-channel.shutdownChannel:
			return
		case now := <-time.After(sleepTime):
			if now.After(nextKeepaliveTime) {
				channel.sendKeepalive()
			}
			if now.After(nextTimeoutTime) {
				channel.checkTimeout(timeout, now)
			}
		}
	}
}
