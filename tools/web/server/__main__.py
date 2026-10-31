"""Executor's tab as a small private service (app.py says what it serves).

    tools/web/server [--config FILE] [serve] [--port N] [--bind ADDR] [--site DIR] [--data DIR]
    tools/web/server --config FILE adduser NAME [--owner]    password on stdin, or asked
    tools/web/server --config FILE passwd NAME | deluser NAME | users
    tools/web/server --config FILE invite [--days N]         prints the code (/login?invite=CODE)
    tools/web/server --config FILE backup DEST               the data directory, while it runs (backup.py)

The configuration: server.example.toml. The first account is the owner's
(adduser --owner); the owner invites the others, from /account or here.
"""
import argparse
import getpass
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from server import app, backup, config  # noqa: E402


def password(args):
    if not sys.stdin.isatty():
        return sys.stdin.readline().rstrip("\n")
    p = getpass.getpass("Password: ")
    if p != getpass.getpass("Again: "):
        sys.exit("they differ")
    return p


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--config")
    sub = ap.add_subparsers(dest="cmd")
    s = sub.add_parser("serve")
    for a in (s, ap):
        a.add_argument("--port", type=int)
        a.add_argument("--bind")
        a.add_argument("--site")
        a.add_argument("--data")
        a.add_argument("--quiet", action="store_true")
    a = sub.add_parser("adduser")
    a.add_argument("name")
    a.add_argument("--owner", action="store_true")
    sub.add_parser("passwd").add_argument("name")
    sub.add_parser("deluser").add_argument("name")
    sub.add_parser("users")
    sub.add_parser("invite").add_argument("--days", type=int, default=7)
    sub.add_parser("backup").add_argument("dest")
    args = ap.parse_args()

    overrides = {"server": {k: getattr(args, k, None) for k in ("port", "bind", "site", "data")}}
    cfg = config.load(args.config, overrides)
    cmd = args.cmd or "serve"
    if cmd == "serve":
        server = app.Server(cfg, quiet=args.quiet)
        host, port = server.server_address[:2]
        print(f"Serving {cfg['server']['site'] or '(no site)'} on http://{host}:{port}/", flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass
        return
    if cmd == "backup":
        dbs, objects = backup.backup(cfg["server"]["data"], os.path.expanduser(args.dest))
        print(f"{args.dest}: {dbs} databases, {objects} new objects")
        return
    accounts = app.App(cfg).accounts
    try:
        if cmd == "adduser":
            accounts.add_user(args.name, password(args), owner=args.owner)
        elif cmd == "passwd":
            accounts.set_password(args.name, password(args))
        elif cmd == "deluser":
            uid = accounts.remove_user(args.name)
            shutil.rmtree(os.path.join(cfg["server"]["data"], "users", str(uid)), ignore_errors=True)
        elif cmd == "users":
            for u in accounts.users():
                print(u["name"] + (" (owner)" if u["owner"] else ""))
        elif cmd == "invite":
            owners = [u for u in accounts.users() if u["owner"]]
            if not owners:
                sys.exit("no owner yet: adduser NAME --owner first")
            print(accounts.new_invite(owners[0]["id"], args.days))
    except ValueError as e:
        sys.exit(str(e))


if __name__ == "__main__":
    main()
