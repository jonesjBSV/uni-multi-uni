package main

import (
	"bufio"
	"fmt"
	"net"
	"os"
	"strconv"
	"strings"
	"time"
)

func main() {
	reader := bufio.NewReader(os.Stdin)
	fmt.Print("Enter port number: ")
	port, _ := reader.ReadString('\n')
	p, _ := strconv.Atoi(strings.TrimSpace(port))

	addr := net.UDPAddr{
		Port: p,
		IP:   net.ParseIP("::"),
	}
	ser, err := net.ListenUDP("udp6", &addr)
	if err != nil {
		fmt.Println(err)
		return
	}
	defer ser.Close()

	buffer := make([]byte, 65507) // Maximum UDP packet size
	var totalBytes int64 = 0
	startTime := time.Now()

	for {
		n, _, err := ser.ReadFromUDP(buffer)
		if err != nil {
			fmt.Println("Error: ", err)
			continue
		}
		totalBytes += int64(n)

		if time.Since(startTime) >= 5*time.Second {
			throughput := float64(totalBytes) / time.Since(startTime).Seconds()
			fmt.Printf("Current throughput: %f bytes/sec\n", throughput)
			startTime = time.Now()
			totalBytes = 0
		}
	}
}
