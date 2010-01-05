/**
 * PCI config space access via mmconfig.
 *
 * This file is part of Vancouver.
 *
 * Vancouver is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Vancouver is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details.
 */

#include "vmm/motherboard.h"
#include <cstdint>

struct AcpiMCFG {
  uint32_t magic;		// MCFG
  uint32_t len;
  uint8_t rev;
  uint8_t checksum;
  char oem_id[6];
  char model_id[8];
  uint32_t oem_rev;
  uint32_t creator_vendor;
  uint32_t creator_utility;
  char _res[8];

  struct Entry {
    uint64_t base;
    uint16_t pci_seg;
    uint8_t pci_bus_start;
    uint8_t pci_bus_end;
    uint32_t _res;
  } __attribute__((packed)) entries[];

} __attribute__((packed));

class PciMMConfigAccess : public StaticReceiver<PciMMConfigAccess>
{
  const char *debug_getname() { return "PciMMConfigAccess"; };

  unsigned  _start_bdf;
  unsigned  _bdf_size;
  unsigned *_mmconfig;

public:

  bool receive(MessagePciConfig &msg) {
    assert(~msg.offset & 3);
    if (!in_range(_start_bdf, msg.bdf, _bdf_size) || msg.offset >= 0x1000) return false;

    unsigned *field = _mmconfig + (msg.bdf << 12) + (msg.offset >> 2);
    
    switch (msg.type) {
    case MessagePciConfig::TYPE_READ:  msg.value = *field; break;
    case MessagePciConfig::TYPE_WRITE: *field = msg.value; break;
    }
    return true;
  }

  
  PciMMConfigAccess(unsigned start_bdf, unsigned bdf_size, unsigned *mmconfig) : _start_bdf(start_bdf), _bdf_size(bdf_size), _mmconfig(mmconfig) 
  {}
};

PARAM(mmconfig,
      {
	MessageAcpi msg("MCFG");
	if (!mb.bus_acpi.send(msg) || !msg.table) {
	  Logging::printf("XXX No MCFG table found.\n");
	  return;
	}
	  
	AcpiMCFG *mcfg = reinterpret_cast<AcpiMCFG *>(msg.table);
	void *mcfg_end = reinterpret_cast<char *>(mcfg) + mcfg->len;

	for (AcpiMCFG::Entry *entry = mcfg->entries; entry < mcfg_end; entry++) {
	  Logging::printf("mmconfig: base 0x%llx seg %02x bus %02x-%02x\n",
			  entry->base, entry->pci_seg,
			  entry->pci_bus_start, entry->pci_bus_end);
	  
	  uint8_t buses = entry->pci_bus_end - entry->pci_bus_start + 1;
	  size_t size = buses * 32 * 8 * 4096;
	  MessageHostOp msg(MessageHostOp::OP_ALLOC_IOMEM, entry->base, size);

	  if (!mb.bus_hostop.send(msg) || !msg.ptr) {
	    Logging::printf("%s failed to allocate iomem %llx+%x\n", __PRETTY_FUNCTION__, entry->base, size);
	    return;
	  }
	  
	  Device *dev = new PciMMConfigAccess((entry->pci_seg << 16) + entry->pci_bus_start * 32 * 8, buses * 32 * 8, reinterpret_cast<unsigned *>(msg.ptr));
	  mb.bus_hwpcicfg.add(dev, &PciMMConfigAccess::receive_static<MessagePciConfig>);
	}
      },
      "mmconfig - provide HW PCI config space access via mmconfig.");