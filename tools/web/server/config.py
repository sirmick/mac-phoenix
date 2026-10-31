"""The configuration file (TOML; server.example.toml has every key)."""
import copy
import os
import tomllib

DEFAULTS = {
    "server": {
        "bind": "127.0.0.1",
        "port": 8090,
        "site": "",                      # the tab: index.html, worker.js, executor-web.wasm, ...
        "data": "~/storage/executor-web",  # accounts and every user's synced data
        "public": False,                 # true: the page without signing in (sync still needs it)
        "secure_cookies": "auto",        # Secure but for localhost; true behind HTTPS
        "trusted_proxies": [],           # addresses whose X-Forwarded-For is believed
        "session_days": 30,
        "max_upload_mb": 64,             # one request's body
    },
    "library": {
        "images": "~/storage/images",
        "include": ["*.img"],
        "exclude": [],
    },
    "relay": {
        "enabled": True,
        # Destination ports (ints, "A-B" ranges): the classic Internet services.
        "ports": [7, 13, 20, 21, 23, 25, 37, 43, 53, 70, 79, 80, 110, 113, 119, 123, 143,
                  194, 443, 563, 993, 995, 1080, 6667, 8080],
        "allow_private": False,          # true: loopback, private and link-local hosts too
        "max_sockets": 32,               # per user, all their tabs together
        "max_connections": 4,            # per user: tabs with the relay open
    },
}


def load(path=None, overrides=None):
    cfg = copy.deepcopy(DEFAULTS)
    if path:
        with open(path, "rb") as f:
            for section, values in tomllib.load(f).items():
                if section not in cfg:
                    raise ValueError(f"{path}: unknown section [{section}]")
                for k, v in values.items():
                    if k not in cfg[section]:
                        raise ValueError(f"{path}: unknown key {section}.{k}")
                    cfg[section][k] = v
    for section, values in (overrides or {}).items():
        cfg[section].update({k: v for k, v in values.items() if v is not None})
    base = os.path.dirname(os.path.abspath(path)) if path else os.getcwd()
    for section, key in (("server", "site"), ("server", "data"), ("library", "images")):
        if cfg[section][key]:
            cfg[section][key] = os.path.join(base, os.path.expanduser(cfg[section][key]))
    return cfg
