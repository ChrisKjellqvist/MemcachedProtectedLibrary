SECTIONS {
/* make sure this is just a 4KB page boundary */
  .text ALIGN(0x8000) : {
    prot.o (.text);
    prot.o (.data);
  }
  PROVIDE(__linker_plib_addr__ = ADDR(.text));
  PROVIDE(__linker_plib_len__ = SIZEOF(.text));
}

SECTIONS {
  .text ALIGN(0x8000) : {
    init.o (.text)
    user_prg.o (.text)
  }

  .data ALIGN(0x8000) : {
    init.o (.data)
    user_prg.o (.data)
  }
}
