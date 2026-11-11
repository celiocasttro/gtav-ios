// gtav - byte layout the game DLL and the runtime must agree on. Included by fs/gtav_pch.h (the
// freestanding copy of the structures) and by src/game_bridge.cpp (the SDK's own headers), so a
// change on either side stops the build instead of corrupting guest state at run time.
#pragma once

#define GTAVFS_CHECK_OFFSET(type, field, value) \
  static_assert(__builtin_offsetof(type, field) == (value), "layout differs from the SDK: " #type "::" #field)

static_assert(sizeof(PPCContext) == 2688 && alignof(PPCContext) == 64, "PPCContext size differs from the SDK");
GTAVFS_CHECK_OFFSET(PPCContext, r3, 0);
GTAVFS_CHECK_OFFSET(PPCContext, r0, 8);
GTAVFS_CHECK_OFFSET(PPCContext, r31, 248);
GTAVFS_CHECK_OFFSET(PPCContext, lr, 256);
GTAVFS_CHECK_OFFSET(PPCContext, ctr, 264);
GTAVFS_CHECK_OFFSET(PPCContext, xer, 272);
GTAVFS_CHECK_OFFSET(PPCContext, reserved, 280);
GTAVFS_CHECK_OFFSET(PPCContext, msr, 288);
GTAVFS_CHECK_OFFSET(PPCContext, cr0, 292);
GTAVFS_CHECK_OFFSET(PPCContext, cr7, 320);
GTAVFS_CHECK_OFFSET(PPCContext, fpscr, 324);
GTAVFS_CHECK_OFFSET(PPCContext, vscr_sat, 328);
GTAVFS_CHECK_OFFSET(PPCContext, last_indirect_target, 332);
GTAVFS_CHECK_OFFSET(PPCContext, f0, 336);
GTAVFS_CHECK_OFFSET(PPCContext, f31, 584);
GTAVFS_CHECK_OFFSET(PPCContext, v0, 592);
GTAVFS_CHECK_OFFSET(PPCContext, v127, 2624);
static_assert(sizeof(PPCCRRegister) == 4 && sizeof(PPCFPSCRRegister) == 4 && sizeof(PPCVRegister) == 16,
              "register sizes differ from the SDK");
static_assert(sizeof(rex::PPCImageInfo) == 48, "PPCImageInfo size differs from the SDK");
GTAVFS_CHECK_OFFSET(rex::PPCImageInfo, func_mappings, 16);
GTAVFS_CHECK_OFFSET(rex::PPCImageInfo, rexcrt_heap, 24);
GTAVFS_CHECK_OFFSET(rex::PPCImageInfo, register_modules, 32);
GTAVFS_CHECK_OFFSET(rex::PPCImageInfo, codegen_flags, 40);
static_assert(sizeof(PPCFuncMapping) == 16, "PPCFuncMapping size differs from the SDK");
