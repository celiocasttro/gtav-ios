/* gtav - stand-in for libxml2.so.2 next to the bundled ld.lld (tools/llvm/lib).

   The official LLVM 19.1.5 Linux build of lld links libxml2 for one feature: merging Windows
   manifests in lld-link (COFF). The installer only ever runs ld.lld (ELF), which never calls these,
   but the dynamic loader still needs a libxml2.so.2 to start lld, and many distributions no longer
   ship that soname. lld's RUNPATH is $ORIGIN/../lib, so this library is found there. Each function
   traps if it is ever reached. Build: tools/release/make_release_linux.sh. */

#define STUB(name) \
  void name(void) { __builtin_trap(); }

STUB(xmlAddChild)
STUB(xmlCopyNamespace)
STUB(xmlDocDumpFormatMemoryEnc)
STUB(xmlDocGetRootElement)
STUB(xmlDocSetRootElement)
STUB(xmlFreeDoc)
STUB(xmlFreeNode)
STUB(xmlFreeNs)
STUB(xmlNewDoc)
STUB(xmlNewNs)
STUB(xmlNewProp)
STUB(xmlReadMemory)
STUB(xmlSetGenericErrorFunc)
STUB(xmlStrdup)
STUB(xmlUnlinkNode)

/* xmlFree is a function pointer variable in libxml2. */
void (*xmlFree)(void*) = 0;
