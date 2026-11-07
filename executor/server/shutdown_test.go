//go:build !windows

package main

import (
	"bufio"
	"io"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"syscall"
	"testing"
	"time"
)

// SIGTERM in the middle of a disk's download: the download finishes
// (the server being replaced cut it short: "Content-Length exceeds
// response Body"), then the server exits.
func TestShutdownFinishesDownloads(t *testing.T) {
	if testing.Short() {
		t.Skip("builds the server")
	}
	dir := t.TempDir()
	bin := filepath.Join(dir, "executor-standalone")
	if out, err := exec.Command("go", "build", "-o", bin, ".").CombinedOutput(); err != nil {
		t.Fatalf("go build: %v\n%s", err, out)
	}
	site, images := filepath.Join(dir, "site"), filepath.Join(dir, "images")
	os.MkdirAll(site, 0o755)
	os.MkdirAll(images, 0o755)
	os.WriteFile(filepath.Join(site, "index.html"), []byte("<p>tab</p>"), 0o644)
	const size = 64 << 20
	os.WriteFile(filepath.Join(images, "big.img"), make([]byte, size), 0o644)
	os.WriteFile(filepath.Join(images, "big.img.json"), []byte(`{}`), 0o644)

	cmd := exec.Command(bin, "--data", filepath.Join(dir, "data"), "--images", images, "--site", site, "--port", "0")
	stdout, _ := cmd.StdoutPipe()
	if err := cmd.Start(); err != nil {
		t.Fatal(err)
	}
	defer cmd.Process.Kill()
	lines := bufio.NewScanner(stdout)
	lines.Scan()
	m := regexp.MustCompile(`on (http://\S+/)`).FindStringSubmatch(lines.Text())
	if m == nil {
		t.Fatalf("no address: %q", lines.Text())
	}
	go io.Copy(io.Discard, stdout)
	base := m[1]
	res, err := http.Get(base)
	if err != nil {
		t.Fatal(err)
	}
	res.Body.Close()
	var cookie *http.Cookie
	for _, c := range res.Cookies() {
		cookie = c
	}
	if cookie == nil {
		t.Fatalf("no claim: %d", res.StatusCode)
	}

	req, _ := http.NewRequest("GET", base+"disks/big.img", nil)
	req.AddCookie(cookie)
	res, err = http.DefaultClient.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	defer res.Body.Close()
	got, _ := io.ReadFull(res.Body, make([]byte, 1<<20))
	cmd.Process.Signal(syscall.SIGTERM)
	time.Sleep(200 * time.Millisecond) // the listener closes; the download goes on
	if _, err := http.Get(base); err == nil {
		t.Errorf("still taking connections")
	}
	rest, err := io.Copy(io.Discard, res.Body)
	if err != nil || int64(got)+rest != size {
		t.Errorf("the download: %d of %d bytes (%v)", int64(got)+rest, size, err)
	}
	done := make(chan error, 1)
	go func() { done <- cmd.Wait() }()
	select {
	case err := <-done:
		if err != nil {
			t.Errorf("exit: %v", err)
		}
	case <-time.After(10 * time.Second):
		t.Errorf("did not exit")
	}
}
