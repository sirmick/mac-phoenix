
# MacPhoenix: the generated sources are committed in generated/, so a C++
# compiler is all the build needs (docs/executor/DIRECTION.md, D3):
# multiversal's API headers, trap glue and struct dumps (Ruby, from
# ../multiversal/defs/*.yaml), the window and control bitmaps (map_to_c,
# *.map), the preferences parser (Bison, prefs/parse.ypp), QuickDraw's
# tables (mkseedtables, mkultable, mksspairtable) and blitters (Perl,
# quickdraw/makerawblt.pl). After editing any of those, configure with
# -DEXECUTOR_REGENERATE=ON (needs ruby, perl and bison): the build (or
# just --target executor-generated) writes them into generated/ again;
# commit the result.
option(EXECUTOR_REGENERATE "Regenerate src/executor/romlib/generated (needs ruby, perl, bison)" OFF)
set(GEN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/generated)
if(EXECUTOR_REGENERATE)
    find_program(PERL_COMMAND perl)
    find_program(BISON_COMMAND bison)
    find_package(Ruby 2.1)
    if(NOT PERL_COMMAND OR NOT BISON_COMMAND OR NOT RUBY_EXECUTABLE)
        message(FATAL_ERROR "EXECUTOR_REGENERATE needs ruby, perl and bison")
    endif()
endif()

find_package(Threads REQUIRED)


if(WIN32 AND MSVC)
    # compiler uses msvc-style command line
    # only clang supported at the moment
    add_compile_options(
        -Xclang -Wall -Xclang -Wextra

        -Wno-unused-parameter
        -Wno-sign-compare
    )
endif()

file(GLOB api_yaml_files
    CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/../multiversal/defs/*.yaml)

file(GLOB multiversal_ruby_sources
    ${CMAKE_CURRENT_SOURCE_DIR}/../multiversal/*.rb)


set(trap_instance_sources ${GEN_DIR}/trap_instances/ReferenceAllTraps.cpp)
set(api_headers)
set(structdump_sources ${GEN_DIR}/structdump/ReferenceAllStructDumps.cpp)
foreach(interfaceModule ${api_yaml_files})
    get_filename_component(base ${interfaceModule} NAME_WE)

        # Special case: MacTypes.h is still around on modern macs.
        # Let's rename our version to avoid conflicts.
    if(${base} STREQUAL MacTypes)
        set(base ExMacTypes)
    endif()
    list(APPEND trap_instance_sources ${GEN_DIR}/trap_instances/${base}.cpp)
    list(APPEND api_headers ${GEN_DIR}/api/${base}.h)
    list(APPEND structdump_sources ${GEN_DIR}/structdump/${base}.cpp)
endforeach()

if(EXECUTOR_REGENERATE)
    # trap_instances.cmake, a listing with this machine's paths, is not
    # used: the list above comes from the YAML files.
    add_custom_command(
        OUTPUT ${api_headers} ${trap_instance_sources} ${structdump_sources}
        DEPENDS
            ${api_yaml_files}
            ${multiversal_ruby_sources}
        COMMAND ${RUBY_EXECUTABLE} make-multiverse.rb -G Executor -o "${GEN_DIR}/"
        COMMAND ${CMAKE_COMMAND} -E rm -f "${GEN_DIR}/trap_instances/trap_instances.cmake"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../multiversal
    )
endif()

if(WIN32)
    set(HOST_OS "win32")
    set(host_os_sources
        config/os/win32/win_serial.cpp
        config/os/win32/win_serial.h
        config/os/win32/win_stat.h
        config/os/win32/win_stat.cpp)
    set(host_os_libs)
elseif(APPLE AND UNIX)
    set(HOST_OS "macosx")
    set(host_os_sources
        config/os/macosx/macosx.mm)
    find_library(COCOA_LIBRARY Cocoa)
    set(host_os_libs ${COCOA_LIBRARY})    
elseif(UNIX AND NOT APPLE)
    set(HOST_OS "linux")
    set(host_os_sources)
    set(host_os_libs)
else()
    set(host_os_sources)
    set(host_os_libs)
endif()
source_group(HostOS FILES ${host_os_sources})

if(CMAKE_COMPILER_IS_GNUCXX)
    # -Wno-enum-compare: Mac OSErr codes are currently spread between different
    #                    anonymous enums in the headers.
    #                    Code like (blah ? noErr : paramErr) triggers the enum-compare warning.

    add_compile_options(
        -Wall
        -Wno-unused-but-set-variable
        -Wno-sign-compare
        -Werror=return-type
        -Wno-enum-compare)
endif()

add_subdirectory(config/front-ends/headless)
add_subdirectory(config/front-ends/phoenix)

function(linkheader from to variant)
    if(DEFINED ARGV3)
        set(input ${ARGV3})
    else()
        set(input ${variant}.h)
    endif()

    configure_file(${from}/${variant}/${input} ${to} COPYONLY)
endfunction()

if(EXECUTOR_REGENERATE)
    add_executable(map_to_c map_to_c/map_to_c.cpp)
    target_include_directories(map_to_c PRIVATE .)
endif()

set(MAP_FILES
        wind/active.map                 ctl/arrow_down_inactive.map
        ctl/arrow_right_active.map      ctl/arrow_up_inactive.map
        wind/ractive.map                wind/zoom.map
        menu/apple.map                  ctl/arrow_left_active.map
        ctl/arrow_right_inactive.map    wind/go_away.map
        ctl/thumb_horiz.map             ctl/arrow_down_active.map
        ctl/arrow_left_inactive.map     ctl/arrow_up_active.map
        wind/grow.map                   ctl/thumb_vert.map
    )
set(GENERATED_FILES)
foreach(mapfile ${MAP_FILES})
    get_filename_component(base ${mapfile} NAME_WE)
    if(EXECUTOR_REGENERATE)
        add_custom_command(
            OUTPUT ${GEN_DIR}/${base}.cmap
            DEPENDS ${mapfile}
            COMMAND map_to_c < ${CMAKE_CURRENT_SOURCE_DIR}/${mapfile} > ${GEN_DIR}/${base}.cmap
            )
    endif()
    list(APPEND GENERATED_FILES ${base}.cmap)
endforeach()

if(EXECUTOR_REGENERATE)
    # No #line directives: they would name this checkout.
    add_custom_command(OUTPUT ${GEN_DIR}/parse.tab.cpp
            DEPENDS prefs/parse.ypp
            COMMAND ${BISON_COMMAND} --no-lines -o ${GEN_DIR}/parse.tab.cpp
                    ${CMAKE_CURRENT_SOURCE_DIR}/prefs/parse.ypp)
endif()
list(APPEND GENERATED_FILES parse.tab.cpp)


set(ctl_sources
    #include/ControlMgr.h
    ctl/ctl.h
    ctl/ctlArrows.cpp ctl/ctlDisplay.cpp ctl/ctlIMIV.cpp ctl/ctlInit.cpp ctl/ctlMisc.cpp
    ctl/ctlMouse.cpp ctl/ctlSet.cpp ctl/ctlSize.cpp ctl/ctlStddef.cpp ctl/ctlPopup.cpp)
source_group(Controls FILES ${ctl_sources})

set(dial_sources
    #include/DialogMgr.h
    dial/dial.h
    dial/itm.h
    dial/dialAlert.cpp dial/dialCreate.cpp dial/dialHandle.cpp dial/dialInit.cpp
    dial/dialManip.cpp dial/dialDispatch.cpp dial/dialItem.cpp)
source_group(Dialogs FILES ${dial_sources})

set(file_sources
    #include/FileMgr.h
    file/file.h
    file/volume.h

    file/fileMisc.cpp
    file/fileHighlevel.cpp  file/fileUnimplemented.cpp
    file/volume.cpp
    
    file/localvolume/localvolume.h 
    file/localvolume/localvolume.cpp 

    file/localvolume/openfile.h

    file/localvolume/item.h 
    file/localvolume/item.cpp

    file/localvolume/itemcache.h
    file/localvolume/itemcache.cpp

    file/localvolume/basilisk.h
    file/localvolume/basilisk.cpp
    file/localvolume/appledouble.h
    file/localvolume/appledouble.cpp
    file/localvolume/macbinary.h
    file/localvolume/macbinary.cpp
    file/localvolume/plain.h
    file/localvolume/plain.cpp
    file/localvolume/mac.h
    file/localvolume/mac.cpp

    file/localvolume/cnidmapper.h
    file/localvolume/simplecnidmapper.h
    file/localvolume/simplecnidmapper.cpp
    )

source_group(Files FILES ${file_sources})

set(hfs_sources 
    hfs/hfs.h
    hfs/hfs_plus.h
    hfs/partition.h
    hfs/drive_flags.h
    hfs/futzwithdosdisks.h
    hfs/hfsBtree.cpp hfs/hfsChanging.cpp hfs/hfsCreate.cpp hfs/hfsFile.cpp
    hfs/hfsHelper.cpp hfs/hfsHier.cpp hfs/hfsMisc.cpp hfs/hfsVolume.cpp
    hfs/hfsWorkingdir.cpp hfs/hfsXbar.cpp hfs/futzwithdosdisks.cpp)
source_group(HFS FILES ${hfs_sources})

set(list_sources
    list/list.h
    list/listAccess.cpp list/listAddDel.cpp list/listCreate.cpp list/listDisplay.cpp
    list/listMouse.cpp list/listOps.cpp list/listStdLDEF.cpp)
source_group(Lists FILES ${list_sources})

set(menu_sources
    menu/menu.h
    menu/menu.cpp menu/menuColor.cpp menu/menuV.cpp menu/stdmdef.cpp menu/stdmbdf.cpp
    menu/sysmenu.cpp)
source_group(Menus FILES ${menu_sources})

set(print_sources
    print/print.h
    print/nextprint.h
    print/ini.h
    print/ini.cpp 
    print/PSstrings.h
    print/PSstrings.cpp print/prError.cpp print/prInit.cpp print/prLowLevel.cpp
    print/prPrinting.cpp print/prRecords.cpp print/PSprint.cpp )
source_group(Printing FILES ${print_sources})

set(qd_sources
    quickdraw/quick.h
    quickdraw/cquick.h    
    quickdraw/depthconv.h
    quickdraw/qcolor.h
    quickdraw/rawblt.h
    quickdraw/gworld.h
    quickdraw/font.h
    quickdraw/picture.h
    quickdraw/region.h
    quickdraw/rgbutil.h
    quickdraw/srcblt.h
    quickdraw/stdbits.h
    quickdraw/xdata.h
    quickdraw/xdblt.h
    quickdraw/image.h
    quickdraw/text.h

    quickdraw/hintemplate.h

    quickdraw/qBit.cpp quickdraw/qCConv.cpp quickdraw/qCGrafPort.cpp
    quickdraw/qCRegular.cpp quickdraw/qColor.cpp quickdraw/qColorMgr.cpp
    quickdraw/qColorutil.cpp quickdraw/qCursor.cpp quickdraw/qGrafport.cpp
    quickdraw/qIMIV.cpp quickdraw/qIMV.cpp quickdraw/qIMVxfer.cpp
    quickdraw/qMisc.cpp quickdraw/qSaveBits.cpp quickdraw/qPaletteMgr.cpp quickdraw/qPen.cpp
    quickdraw/qPicstuff.cpp quickdraw/qPicture.cpp quickdraw/qPixMapConv.cpp
    quickdraw/qPoint.cpp quickdraw/qPoly.cpp quickdraw/qRect.cpp
    quickdraw/qRegion.cpp quickdraw/qRegular.cpp quickdraw/qScale.cpp
    quickdraw/qStandard.cpp quickdraw/qStdArc.cpp quickdraw/qStdBits.cpp
    quickdraw/qStdLine.cpp quickdraw/qStdOval.cpp quickdraw/qStdPic.cpp
    quickdraw/qStdPoly.cpp quickdraw/qStdRRect.cpp quickdraw/qStdRect.cpp
    quickdraw/qStdRgn.cpp quickdraw/qStdText.cpp quickdraw/qText.cpp
    quickdraw/qGWorld.cpp quickdraw/qGDevice.cpp quickdraw/qIMVI.cpp
    quickdraw/qHooks.cpp quickdraw/xdata.cpp quickdraw/xdblt.cpp
    quickdraw/rawpatblt.cpp quickdraw/rawsrcblt.cpp quickdraw/srcblt.cpp
     quickdraw/qPict2.cpp quickdraw/image.cpp
    quickdraw/image_inits.cpp quickdraw/default_ctab_values.cpp
    quickdraw/dcconvert.cpp quickdraw/rgbutil.cpp quickdraw/dcmaketables.cpp
    quickdraw/fontIMVI.cpp quickdraw/font.cpp
    quickdraw/displays.cpp
)

source_group(QuickDraw FILES ${qd_sources})

if(EXECUTOR_REGENERATE)
    add_executable(mkseedtables quickdraw/mkseedtables/mkseedtables.c)
    add_executable(mkultable quickdraw/mkultable/mkultable.c)
    add_executable(mksspairtable quickdraw/mksspairtable/mksspairtable.c)
    add_custom_command(OUTPUT ${GEN_DIR}/seedtables.ctable COMMAND mkseedtables > ${GEN_DIR}/seedtables.ctable)
    add_custom_command(OUTPUT ${GEN_DIR}/ultable.ctable COMMAND mkultable > ${GEN_DIR}/ultable.ctable)
    add_custom_command(OUTPUT ${GEN_DIR}/sspairtable.ctable COMMAND mksspairtable > ${GEN_DIR}/sspairtable.ctable)

    add_custom_command(OUTPUT ${GEN_DIR}/rawpatstubs.ctable
        DEPENDS quickdraw/makerawblt.pl quickdraw/pat-blitters.tmpl
        COMMAND ${PERL_COMMAND} ${CMAKE_CURRENT_SOURCE_DIR}/quickdraw/makerawblt.pl
            < ${CMAKE_CURRENT_SOURCE_DIR}/quickdraw/pat-blitters.tmpl
            > ${GEN_DIR}/rawpatstubs.ctable)

    add_custom_command(OUTPUT ${GEN_DIR}/rawsrcstubs.ctable
        DEPENDS quickdraw/makerawblt.pl quickdraw/src-blitters.tmpl
        COMMAND ${PERL_COMMAND} ${CMAKE_CURRENT_SOURCE_DIR}/quickdraw/makerawblt.pl
            < ${CMAKE_CURRENT_SOURCE_DIR}/quickdraw/src-blitters.tmpl
            > ${GEN_DIR}/rawsrcstubs.ctable)
endif()
list(APPEND GENERATED_FILES seedtables.ctable ultable.ctable sspairtable.ctable)
list(APPEND GENERATED_FILES rawpatstubs.ctable rawsrcstubs.ctable)

list(TRANSFORM GENERATED_FILES PREPEND ${GEN_DIR}/)
if(EXECUTOR_REGENERATE)
    # cmake --build BUILD --target executor-generated: just the files.
    add_custom_target(executor-generated DEPENDS
        ${GENERATED_FILES} ${api_headers} ${trap_instance_sources} ${structdump_sources})
endif()
source_group(Generated FILES ${GENERATED_FILES})

set(res_sources
    res/resource.h

    res/resGet.cpp res/resGetinfo.cpp res/resGettype.cpp res/resIMIV.cpp
    res/resInit.cpp res/resMisc.cpp res/resMod.cpp res/resOpen.cpp res/resPolicy.cpp
    res/resPrivate.cpp
    drag.cpp
    res/resSetcur.cpp res/resPartial.cpp) 

source_group(Resources FILES ${res_sources})

set(te_sources
    textedit/tesave.h
    textedit/textedit.h

    textedit/teAccess.cpp textedit/teDisplay.cpp textedit/teEdit.cpp 
    textedit/teIMIV.cpp textedit/teIMV.cpp textedit/teInit.cpp
    textedit/teInsert.cpp textedit/teMisc.cpp textedit/teScrap.cpp)
source_group(TextEdit FILES ${te_sources})

set(wind_sources
    wind/wind.h
    
    wind/windColor.cpp wind/windDisplay.cpp wind/windDocdef.cpp
    wind/windInit.cpp wind/windMisc.cpp wind/windMouse.cpp wind/windSize.cpp
    wind/windUpdate.cpp wind/windRootless.cpp)
source_group(Windows FILES ${wind_sources})

set(ae_sources
    appleevent/apple_events.h
    appleevent/AE.cpp appleevent/AE_desc.cpp appleevent/AE_hdlr.cpp
    appleevent/AE_coercion.cpp)
source_group(AppleEvents FILES ${ae_sources})

set(sound_sources
    sound/sounddriver.h
    sound/soundopts.h
    sound/soundfake.h
    
    sound/sounddriver.cpp sound/sound.cpp sound/soundIMVI.cpp sound/soundfake.cpp
    sound/snth5.cpp

    SpeechManager.cpp sound/snddispatch.cpp)
if(HOST_OS STREQUAL macosx)
set(sound_sources ${sound_sources} SpeechManager-MacBridge.mm)
endif()
source_group(Sound FILES ${sound_sources})

set(num_sources
    sane/float_fcw.h sane/float.h sane/floatconv.h
    sane/float4.cpp sane/float5.cpp sane/float7.cpp sane/floatnext.cpp)
source_group(Numerics FILES ${num_sources})


set(base_sources
        # include/ExMacTypes.h
        
        base/common.h
        base/mactype.h
        base/lowglobals.h
        base/functions.h
        base/functions.impl.h
        base/byteswap.h

        base/m68kint.h
        base/cpu.h
        base/cpu.cpp    

        base/traps.h
        base/traps.impl.h
        base/trapname.h
        base/dispatcher.h
        base/trapglue.h
        base/logging.h
        base/api-module.h
        base/emustubs.h
    
        base/trapname.cpp
        base/traps.cpp
        base/dispatcher.cpp
        base/trap_history.cpp
        base/emustubs.cpp
        base/logging.cpp
        base/structdump.h
        base/structdump.cpp
    
        base/builtinlibs.h
        base/builtinlibs.cpp

        base/debugger.h
        base/debugger.cpp

        base/fault_handler.h
        base/fault_handler.cpp

        base/patches.cpp
    )
source_group(Base FILES ${base_sources})

set(vdriver_sources
        vdriver/vdriver.cpp
        vdriver/refresh.cpp
        vdriver/videodrvr.cpp
        vdriver/autorefresh.cpp 
        vdriver/dirtyrect.cpp
        vdriver/dirtyrect.h
        vdriver/autorefresh.h
        vdriver/refresh.h
        vdriver/vdriver.h
        vdriver/eventsink.cpp
        vdriver/eventrecorder.h
        vdriver/eventrecorder.cpp
    )
source_group(VDriver FILES ${vdriver_sources})

set(mman_sources
    mman/memsize.h
    mman/mman.h
    mman/mman_private.h
    mman/tempalloc.h

    mman/mman.cpp mman/mmansubr.cpp mman/whatis.cpp
    mman/tempmem.cpp
)
source_group(MMan FILES ${mman_sources})

set(time_sources
        time/time.h
        time/vbl.h
        time/syncint.h

        time/time.cpp
        time/vbl.cpp
        time/syncint.cpp
    )
source_group(Time FILES ${time_sources})

set(osevent_sources
        osevent/osevent.h
        osevent/hle.cpp
        osevent/ibm_keycodes.cpp
        osevent/osevent.cpp
    )
source_group(OSEvent FILES ${osevent_sources})

set(error_sources
    error/error.h error/syserr.h error/system_error.h
    error/error.cpp error/syserr.cpp error/system_error.cpp
)

source_group(Error FILES ${error_sources})

set(commandline_sources
    commandline/options.h
    commandline/options.cpp
    commandline/option_arguments.h
    commandline/flags.h
    )

source_group(Commandline FILES ${commandline_sources})

set(prefs_sources
    prefs/prefs.h
    prefs/parse.h
    prefs/options.h
    prefs/crc.h

    prefs/parse.ypp
    prefs/crc.cpp
    prefs/options.cpp
    prefs/prefs.cpp
)

source_group(Prefs FILES ${prefs_sources})

set(mactcp_sources
    mactcp/mactcp.cpp
    mactcp/dnr.cpp
)

source_group(MacTCP FILES ${mactcp_sources})

set(opentransport_sources
    opentransport/ot.cpp
    opentransport/ot_client.cpp
    opentransport/ot_provider.cpp
    opentransport/ot_inet.cpp
    opentransport/ot_utils.cpp
    opentransport/ot_slm.cpp
    opentransport/ot_tasks.cpp
    opentransport/ot_options.cpp
    net/hostdns.cpp
)

source_group(OpenTransport FILES ${opentransport_sources})

set(util_sources
    util/macstrings.h
    util/macstrings.cpp

    util/string.h
    util/string.cpp
    
    util/uniquefile.h
    util/uniquefile.cpp

    util/handle_vector.h
    util/static_vector.h
)

source_group(Util FILES ${util_sources})



set(mpw_sources
    mpw/mpw.h
    mpw/mpw.cpp
    )
source_group(MPW FILES ${mpw_sources})


set(misc_sources
  desk.cpp extensions.cpp sysroutines.cpp helppkg.cpp textservices.cpp component.cpp device.cpp disk.cpp diskinit.cpp dump.cpp
  gestalt.cpp iu.cpp launch.cpp  notify.cpp 
   osutil.cpp pack.cpp scrap.cpp script.cpp segment.cpp serial.cpp hostdisk.cpp
  stdfile.cpp stdfilepkg.cpp romlib_stubs.cpp  toolevent.cpp toolutil.cpp 
   aboutbox.cpp
  version.cpp shutdown.cpp screen-dump.cpp
  process.cpp alias.cpp edition.cpp  balloon.cpp
   adb.cpp color_wheel_bits.cpp finder.cpp  
  icon.cpp redrawscreen.cpp quicktime.cpp
  commtool.cpp cfm.cpp pef_hash.cpp
  mixed_mode.cpp appearance.cpp
  executor.cpp  
  prefpanel.cpp
  qColorPicker.cpp
  bindec.cpp 
  toolmath.cpp
  init.cpp
  )


set(include_sources 
                  include/rsys/aboutbox.h
                  include/rsys/adb.h
                  include/rsys/alias.h
                  include/rsys/appearance.h
                  include/rsys/arrowkeys.h
                  include/rsys/color_wheel_bits.h
                  include/rsys/desk.h
                  include/rsys/device.h
                  include/rsys/dump.h
                  include/rsys/evil.h
                  include/rsys/executor.h
                  include/rsys/filesystem.h
                  include/rsys/gestalt.h
                  include/rsys/hook.h
                  include/rsys/icon.h
                  include/rsys/keyboard.h
                  include/rsys/launch.h
                  include/rsys/mactcp.h
                  include/rsys/opentransport.h
                  include/rsys/macros.h
                  include/rsys/noreturn.h
                  include/rsys/osutil.h
                  include/rsys/paths.h
                  include/rsys/prefpanel.h
                  include/rsys/process.h
                  include/rsys/redrawscreen.h
                  include/rsys/safe_alloca.h
                  include/rsys/scrap.h
                  include/rsys/screen-dump.h
                  include/rsys/segment.h
                  include/rsys/serial.h
                  include/rsys/unixio.h
                  include/rsys/stdfile.h
                  include/rsys/toolevent.h
                  include/rsys/toolutil.h
                  include/rsys/version.h
                  mkvol/mkvol.h
                  mkvol/mkvol_internal.h)

source_group(APIHeaders FILES ${api_headers})
source_group(TrapInstances FILES ${trap_instance_sources})
source_group(StructDump FILES ${structdump_sources})

set(sources ${base_sources} ${mman_sources} ${vdriver_sources}
    ${ctl_sources} ${dial_sources} 
    ${list_sources} ${menu_sources}
    ${print_sources} ${qd_sources} ${res_sources}
    ${te_sources} ${wind_sources} ${ae_sources}
    ${sound_sources} ${num_sources} ${misc_sources}
    ${file_sources} ${hfs_sources} ${time_sources} ${osevent_sources}
    ${error_sources} ${commandline_sources} ${prefs_sources}
    ${mactcp_sources}
    ${opentransport_sources}
    ${util_sources}
    ${api_headers} ${trap_instance_sources} ${structdump_sources} ${host_os_sources}
    ${mpw_sources}
    ${include_sources})

# MacPhoenix: the host network edge (include/rsys/hostnet.h): host sockets,
# or none (MacTCP and Open Transport answer with errors; no host threads).
set(EXECUTOR_NET posix CACHE STRING "Executor's network backend: posix | none")
set_property(CACHE EXECUTOR_NET PROPERTY STRINGS posix none)
if(NOT EXECUTOR_NET MATCHES "^(posix|none)$")
    message(FATAL_ERROR "EXECUTOR_NET must be posix or none, not ${EXECUTOR_NET}")
endif()

add_library(romlib ${sources}
        ${GENERATED_FILES}
        ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/minicoro/minicoro.c
        host/${EXECUTOR_NET}/net.cpp
    )

target_include_directories(romlib PUBLIC . include/ ${GEN_DIR}/api/ ${GEN_DIR})
target_include_directories(romlib PUBLIC config/os/${HOST_OS})



# MacPhoenix: processes and Thread Manager threads are minicoro coroutines.
target_include_directories(romlib PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/minicoro)
target_link_libraries(romlib Threads::Threads executor_cpu)


target_link_libraries(romlib resources)
find_library(RESOLV_LIBRARY resolv)
if(RESOLV_LIBRARY)
    target_link_libraries(romlib ${RESOLV_LIBRARY})  # MacPhoenix: res_query (OT's DNS queries)
endif()
target_link_libraries(romlib ${host_os_libs})

if(APPLE)
    set(platform_resource_files ../icon/executor.icns)
    set_source_files_properties(../icon/executor.icns PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
else()
    set(platform_resource_files)
endif()

