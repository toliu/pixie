/*
 * Copyright 2018- The Pixie Authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

package main

import (
	"compress/gzip"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"strconv"
	"strings"
)

// Gzip handling adapted from https://gist.github.com/the42/1956518
type gzipResponseWriter struct {
	io.Writer
	http.ResponseWriter
}

func (w gzipResponseWriter) Write(b []byte) (int, error) {
	return w.Writer.Write(b)
}

func optionallyGzipMiddleware(next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if !strings.Contains(r.Header.Get("Accept-Encoding"), "gzip") {
			next(w, r)
			return
		}
		w.Header().Set("Content-Encoding", "gzip")
		w.Header().Set("Content-Type", "text/plain")
		gz := gzip.NewWriter(w)
		defer gz.Close()
		gzr := gzipResponseWriter{Writer: gz, ResponseWriter: w}
		next(gzr, r)
	}
}

type httpContent struct {
	headers map[string]string
	body    string
}

func buildHTTPContent(numBytesHeaders int, numBytesBody int, char string) *httpContent {
	headers := make(map[string]string)
	// TODO(james): add random headers.
	return &httpContent{
		body:    strings.Repeat(char, numBytesBody),
		headers: headers,
	}
}

func makeSimpleServeFunc(numBytesHeaders int, numBytesBody int) http.HandlerFunc {
	content := buildHTTPContent(numBytesHeaders, numBytesBody, "s")
	return func(w http.ResponseWriter, r *http.Request) {
		// Force content to not be chunked.
		bytesWritten, err := fmt.Fprint(w, content.body)
		w.Header().Set("Content-Length", fmt.Sprintf("%d", bytesWritten))
		if err != nil {
			log.Println("error")
		}
	}
}

// Chunked+GZip not currently supported.
func makeChunkedServeFunc(numBytesHeaders int, numBytesBody int, numChunks int) http.HandlerFunc {
	content := buildHTTPContent(numBytesHeaders, numBytesBody, "c")
	chunkedBody := make([]string, numChunks)
	chunkSize := len(content.body) / numChunks
	for i := 0; i < numChunks-1; i++ {
		chunkedBody[i] = content.body[i*chunkSize : (i+1)*chunkSize]
	}
	chunkedBody[numChunks-1] = content.body[(numChunks-1)*chunkSize:]

	return func(w http.ResponseWriter, r *http.Request) {
		flusher, ok := w.(http.Flusher)
		if !ok {
			panic("http.ResponseWriter should be an http.Flusher")
		}
		for _, chunk := range chunkedBody {
			_, err := fmt.Fprint(w, chunk)
			if err != nil {
				log.Println("error")
			}
			flusher.Flush()
		}
	}
}

func main() {
	numBytesHeaders, err := strconv.Atoi(os.Getenv("NUM_BYTES_HEADERS"))
	if err != nil {
		log.Fatalln("Must specify valid integer NUM_BYTES_HEADERS in environment")
	}
	numBytesBody, err := strconv.Atoi(os.Getenv("NUM_BYTES_BODY"))
	if err != nil {
		log.Fatalln("Must specify valid integer NUM_BYTES_BODY in environment")
	}

	http.HandleFunc("/", optionallyGzipMiddleware(makeSimpleServeFunc(numBytesHeaders, numBytesBody)))
	http.HandleFunc("/chunked", makeChunkedServeFunc(numBytesHeaders, numBytesBody, 10))

	port := os.Getenv("PORT")
	if err := http.ListenAndServe(fmt.Sprintf(":%s", port), nil); err != nil {
		panic("HTTP server failed")
	}
}