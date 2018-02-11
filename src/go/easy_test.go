package mocha

import (
	"log"
	"net/http"
	_ "net/http/pprof"
	"testing"
)

func TestServerClientEasy(t *testing.T) {
	config := ChannelConfig{
		Address: "localhost:9292",
	}
	server, err1 := Bind(&config)
	if err1 != nil {
		panic(err1)
	}

	go http.ListenAndServe(":10000", nil)

	go serve(server)

	client, err2 := ConnectEasy(&config)
	if err2 != nil {
		panic(err2)
	}

	for idx := 0; idx < 10; idx++ {
		resp, err3 := client.Invoke(int32(idx), NewHeaderMust([]byte("abc"), []byte("def"), []byte("123"), []byte("456")), nil)
		log.Printf("Done invoking %v %v", resp, err3)
	}

	server.Close()
}
