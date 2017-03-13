package mocharpc

import (
	"log"
	"os"
	"os/signal"
	"runtime"
	"syscall"
	"testing"
	"time"
)

type A struct {
	a int
}

type B struct {
	A
	b int
}

/*
func TestConnect(t *testing.T) {
  config := defaultConfig()
  config.Address = "localhost:9090"
  channel, _ := Connect(config)
  var code int32 = 0

  for {
    select {
    case request, running := <-channel.Request:
      if running {
        log.Printf("received request %v", request)
      } else {
        return
      }
    case response, running := <-channel.Response:
      if running {
        log.Printf("received response %v", response)
      } else {
        return
      }
    case <-time.After(time.Second * 1):
      id := channel.SendRequest(code, NewHeaderUnsafe("abc", "def", "123", "456"), nil)
      log.Printf("send request %v", id)
      code += 1
      if code >= 3 {
        channel.Close()
      }
    }
  }
}
*/

func serveClient(client *Channel) {
	for {
		select {
		case request, running := <-client.Request:
			if running {
				//log.Printf("received request %v from client", request)
				client.SendResponse(request.ID, request.Code+100, request.Header, request.Payload)
			} else {
				return
			}
		case _, running := <-client.Response:
			if running {
				//log.Printf("received response %v from client", response)
			} else {
				return
			}
		}
	}
}

func serve(server *ServerChannel) {
	for {
		if client, err := server.Accept(); err != nil {
			log.Println("Could not accept client ", err)
		} else {
			go serveClient(client)
		}
	}
}

func TestServerOnly(t *testing.T) {
	config := ChannelConfig{
		Address: "localhost:9191",
	}
	server, err1 := Bind(&config)
	log.Println("Server ", err1)
	go func() {
		sigs := make(chan os.Signal, 1)
		signal.Notify(sigs, syscall.SIGQUIT)
		buf := make([]byte, 1<<20)
		for {
			<-sigs
			stacklen := runtime.Stack(buf, true)
			log.Printf("=== received SIGQUIT ===\n*** goroutine dump...\n%s\n*** end\n", buf[:stacklen])
		}
	}()
	go serve(server)
	time.Sleep(1000 * time.Second)
	server.Close()
}

func TestServerClient(t *testing.T) {
	config := ChannelConfig{
		Address: "localhost:9191",
	}
	server, err1 := Bind(&config)
	log.Println("Server ", err1)
	go func() {
		sigs := make(chan os.Signal, 1)
		signal.Notify(sigs, syscall.SIGQUIT)
		buf := make([]byte, 1<<20)
		for {
			<-sigs
			stacklen := runtime.Stack(buf, true)
			log.Printf("=== received SIGQUIT ===\n*** goroutine dump...\n%s\n*** end\n", buf[:stacklen])
		}
	}()
	var code int32

	go serve(server)

	client, err2 := Connect(&config)
	log.Println("Client ", err2)

	for {
		select {
		case _, running := <-client.Request:
			if running {
				//log.Printf("received request %v", request)
			} else {
				return
			}
		case _, running := <-client.Response:
			if running {
				//log.Printf("received response %v from server", response)
			} else {
				return
			}
		case <-time.After(time.Second * 1):
			for idx := 0; idx < 10000; idx++ {
				client.SendRequest(code, NewHeaderUnsafe("abc", "def", "123", "456"), nil)
			}
			log.Printf("Done sending request")
			code++
			if code >= 10 {
				client.Close()
				goto exitTest
			}
		}
	}
exitTest:

	server.Close()
}
