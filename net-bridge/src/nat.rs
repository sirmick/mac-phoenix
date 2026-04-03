//! NAT table: maps (protocol, mac_port) → (dst_ip, dst_port)

use std::collections::HashMap;
use std::net::Ipv4Addr;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct NatKey {
    pub proto: u8, // 6=TCP, 17=UDP
    pub mac_port: u16,
}

#[derive(Debug, Clone)]
pub struct NatEntry {
    pub dst_ip: Ipv4Addr,
    pub dst_port: u16,
    pub src_ip: Ipv4Addr,
}

pub struct NatTable {
    entries: HashMap<NatKey, NatEntry>,
}

impl NatTable {
    pub fn new() -> Self {
        Self {
            entries: HashMap::new(),
        }
    }

    pub fn insert(&mut self, proto: u8, mac_port: u16, dst_ip: Ipv4Addr, dst_port: u16, src_ip: Ipv4Addr) {
        let key = NatKey { proto, mac_port };
        self.entries.insert(key, NatEntry { dst_ip, dst_port, src_ip });
    }

    pub fn lookup(&self, proto: u8, mac_port: u16) -> Option<&NatEntry> {
        self.entries.get(&NatKey { proto, mac_port })
    }

    pub fn remove(&mut self, proto: u8, mac_port: u16) {
        self.entries.remove(&NatKey { proto, mac_port });
    }
}
