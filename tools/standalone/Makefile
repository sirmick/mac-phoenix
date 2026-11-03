# executor-standalone: the Go server with the tab embedded
# (docs/executor/Standalone.md).
#
#   make [WASM_BUILD=DIR]      site/ from DIR (make_site.sh), then ./executor-standalone
#   make dist [VERSION=V]      dist/: executor-standalone-V-OS-ARCH[.exe] for six targets,
#                              SHA256SUMS and README.txt (README.dist.txt); V: git describe
#   make test [OUT=PATH]       the binary, and go test (the API, the claim, the relay, the program
#                              itself; ctest executor.web.standalone_server in build-wasm)
#   make nosite                the binary with no tab (site/ as it is)
#
# The site's large files are embedded as their .gz only (static.go
# decompresses for a browser that wants them plain).
ROOT       := $(abspath ../..)
WASM_BUILD ?= $(ROOT)/build-wasm
GO         ?= go
VERSION    ?= $(shell git -C $(ROOT) describe --always --dirty 2>/dev/null || echo dev)
LDFLAGS    := -s -w -X main.version=$(VERSION)
TARGETS    := linux/amd64 linux/arm64 darwin/amd64 darwin/arm64 windows/amd64 windows/arm64
GOFLAGS    := -trimpath
OUT        ?= executor-standalone
export CGO_ENABLED=0

.PHONY: all site nosite dist test clean executor-standalone
all: site executor-standalone

site:
	find site -mindepth 1 ! -name .keep -delete
	$(ROOT)/tools/web/make_site.sh site $(WASM_BUILD)
	rm -f site/executor-web.wasm site/executor-web.js

executor-standalone nosite:  # (go build knows what changed, embedded files too)
	$(GO) build $(GOFLAGS) -ldflags '$(LDFLAGS)' -o $(OUT) .

dist: site
	rm -rf dist && mkdir dist
	set -e; for t in $(TARGETS); do \
	    os=$${t%/*}; arch=$${t#*/}; ext=; [ $$os = windows ] && ext=.exe; \
	    GOOS=$$os GOARCH=$$arch $(GO) build $(GOFLAGS) -ldflags '$(LDFLAGS)' \
	        -o dist/executor-standalone-$(VERSION)-$$os-$$arch$$ext .; \
	done
	sed 's/VERSION/$(VERSION)/g' README.dist.txt > dist/README.txt
	cd dist && sha256sum executor-standalone-* > SHA256SUMS
	ls -l dist

test: executor-standalone
	$(GO) test ./...

clean:
	rm -rf dist executor-standalone
	find site -mindepth 1 ! -name .keep -delete
