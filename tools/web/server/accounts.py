"""Local accounts: the owner and the people they invite.

accounts.sqlite in the data directory: users (name, scrypt hash, owner
or not), invites (a code, by whom, until when, used by whom) and sessions
(a cookie's token, by its SHA-256: the database alone lets nobody in).
"""
import hashlib
import hmac
import re
import secrets
import sqlite3
import threading
import time

NAME = re.compile(r"[A-Za-z0-9_.-]{1,32}")
MIN_PASSWORD = 8
SCRYPT = dict(n=1 << 14, r=8, p=1)
# A hash takes 16 MB and some 50 ms: two at a time, the rest wait.
HASHING = threading.BoundedSemaphore(2)

SCHEMA = """
create table if not exists users (
    id integer primary key, name text unique not null, hash text not null,
    owner integer not null default 0, created integer not null);
create table if not exists invites (
    code text primary key, created_by integer not null, created integer not null,
    expires integer not null, used_by integer);
create table if not exists sessions (
    token text primary key, user_id integer not null, expires integer not null);
"""


def hash_password(password):
    salt = secrets.token_bytes(16)
    with HASHING:
        h = hashlib.scrypt(password.encode(), salt=salt, dklen=32, **SCRYPT)
    return f"scrypt${SCRYPT['n']}${SCRYPT['r']}${SCRYPT['p']}${salt.hex()}${h.hex()}"


def check_password(password, stored):
    try:
        kind, n, r, p, salt, h = stored.split("$")
        if kind != "scrypt":
            return False
        with HASHING:
            got = hashlib.scrypt(password.encode(), salt=bytes.fromhex(salt), dklen=len(h) // 2,
                                 n=int(n), r=int(r), p=int(p))
        return hmac.compare_digest(got.hex(), h)
    except ValueError:
        return False


_dummy = []


def dummy_hash():
    """A hash to check against for a name nobody has: the same time either way."""
    if not _dummy:
        _dummy.append(hash_password(secrets.token_hex(16)))
    return _dummy[0]


def _digest(token):
    return hashlib.sha256(token.encode()).hexdigest()


class Accounts:
    def __init__(self, path):
        self.db = sqlite3.connect(path, check_same_thread=False)
        self.db.row_factory = sqlite3.Row
        self.lock = threading.Lock()
        with self.lock, self.db:
            self.db.executescript(SCHEMA)
        dummy_hash()

    @staticmethod
    def _user(row):
        return {"id": row["id"], "name": row["name"], "owner": bool(row["owner"])} if row else None

    @staticmethod
    def check_new(name, password):
        if not NAME.fullmatch(name or ""):
            raise ValueError("a name is 1 to 32 letters, digits, '.', '_' or '-'")
        if len(password or "") < MIN_PASSWORD:
            raise ValueError(f"a password is at least {MIN_PASSWORD} characters")

    def add_user(self, name, password, owner=False):
        self.check_new(name, password)
        hashed = hash_password(password)          # outside the lock: it takes a while
        with self.lock, self.db:
            try:
                cur = self.db.execute("insert into users (name, hash, owner, created) values (?, ?, ?, ?)",
                                      (name, hashed, int(owner), int(time.time())))
            except sqlite3.IntegrityError:
                raise ValueError(f"{name} exists already")
        return {"id": cur.lastrowid, "name": name, "owner": owner}

    def set_password(self, name, password):
        self.check_new(name, password)
        hashed = hash_password(password)
        with self.lock, self.db:
            cur = self.db.execute("update users set hash = ? where name = ?", (hashed, name))
            if not cur.rowcount:
                raise ValueError(f"no user {name}")
            # Signed out everywhere.
            self.db.execute("delete from sessions where user_id = (select id from users where name = ?)", (name,))

    def remove_user(self, name):
        with self.lock, self.db:
            row = self.db.execute("select id from users where name = ?", (name,)).fetchone()
            if not row:
                raise ValueError(f"no user {name}")
            self.db.execute("delete from sessions where user_id = ?", (row["id"],))
            self.db.execute("delete from users where id = ?", (row["id"],))
        return row["id"]

    def users(self):
        with self.lock:
            return [self._user(r) for r in self.db.execute("select * from users order by id")]

    def check(self, name, password):
        with self.lock:
            row = self.db.execute("select * from users where name = ?", (name,)).fetchone()
        if not row:
            check_password(password, dummy_hash())
            return None
        return self._user(row) if check_password(password, row["hash"]) else None

    def new_session(self, user_id, days):
        token = secrets.token_urlsafe(32)
        with self.lock, self.db:
            self.db.execute("delete from sessions where expires < ?", (int(time.time()),))
            self.db.execute("insert into sessions values (?, ?, ?)",
                            (_digest(token), user_id, int(time.time() + days * 86400)))
        return token

    def session(self, token):
        if not token:
            return None
        with self.lock:
            row = self.db.execute(
                "select users.* from sessions join users on users.id = sessions.user_id"
                " where token = ? and expires >= ?", (_digest(token), int(time.time()))).fetchone()
        return self._user(row)

    def end_session(self, token):
        with self.lock, self.db:
            self.db.execute("delete from sessions where token = ?", (_digest(token or ""),))

    def new_invite(self, by, days=7):
        code = secrets.token_urlsafe(12)
        now = int(time.time())
        with self.lock, self.db:
            self.db.execute("insert into invites values (?, ?, ?, ?, null)", (code, by, now, now + days * 86400))
        return code

    def invites(self):
        with self.lock:
            rows = self.db.execute(
                "select invites.*, users.name as used_name from invites"
                " left join users on users.id = invites.used_by order by created").fetchall()
        return [{"code": r["code"], "created": r["created"], "expires": r["expires"],
                 "used_by": r["used_name"]} for r in rows]

    def invite_valid(self, code):
        with self.lock:
            return self.db.execute("select 1 from invites where code = ? and used_by is null and expires >= ?",
                                   (code or "", int(time.time()))).fetchone() is not None

    def register(self, code, name, password):
        self.check_new(name, password)
        hashed = hash_password(password)
        with self.lock, self.db:
            row = self.db.execute("select * from invites where code = ? and used_by is null and expires >= ?",
                                  (code or "", int(time.time()))).fetchone()
            if not row:
                raise ValueError("that invitation is not valid (used, expired or mistyped)")
            try:
                cur = self.db.execute("insert into users (name, hash, owner, created) values (?, ?, 0, ?)",
                                      (name, hashed, int(time.time())))
            except sqlite3.IntegrityError:
                raise ValueError(f"{name} is taken")
            self.db.execute("update invites set used_by = ? where code = ?", (cur.lastrowid, code))
        return {"id": cur.lastrowid, "name": name, "owner": False}
