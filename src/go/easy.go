package mocha

import (
	"net"
)

type ChannelEasy struct {
	channel *Channel
}

func ConnectEasyChannel(channel *Channel) *ChannelEasy {
	return &ChannelEasy{channel: channel}
}

func ConnectEasyConn(conn net.Conn, config *ChannelConfig) (*ChannelEasy, error) {
	if c, e := ConnectConn(conn, config); e != nil {
		return nil, e
	} else {
		return ConnectEasyChannel(c), nil
	}
}

func ConnectEasy(config *ChannelConfig) (*ChannelEasy, error) {
	if c, e := Connect(config); e != nil {
		return nil, e
	} else {
		return ConnectEasyChannel(c), nil
	}
}

func (c *ChannelEasy) SendRequest(code int32, header []PacketHeader, payload []uint8) (int64, error) {
	return c.channel.SendRequest(code, header, payload)
}

func (c *ChannelEasy) Poll() (packet *Packet, err error) {
	var ok bool
	select {
	case packet, ok = <-c.channel.Request:
	case packet, ok = <-c.channel.Response:
	}

	if !ok {
		err = errChannelClosed
	}
	return
}

func (c *ChannelEasy) SendResponse(id int64, code int32, header []PacketHeader, payload []uint8) error {
	return c.channel.SendResponse(id, code, header, payload)
}

func (c *ChannelEasy) Invoke(code int32, header []PacketHeader, payload []uint8) (Response, error) {
	if id, err := c.SendRequest(code, header, payload); err != nil {
		return nil, err
	} else {
		for {
			if packet, err := c.Poll(); err != nil {
				return nil, err
			} else {
				if response := packet.IsResponse(); response != nil && id == response.ID {
					return response, nil
				}
				var packetType string
				if packet.isRequest() {
					packetType = "request"
				} else {
					packetType = "response"
				}
				c.channel.logger.Printf("[E]: Unexpected %s: id=%d, code=%d, headers=%v, %d bytes payload '%v'", packetType,
					packet.ID, packet.Code, packet.Header, packet.Payload)
			}
		}
	}
}

func (c *ChannelEasy) Close() {
	c.channel.Close()
}
