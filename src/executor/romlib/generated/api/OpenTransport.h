/*****************************************************************************
 *                                                                           *
 *                              OpenTransport.h                              *
 *                              ===============                              *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    Boolean                                                 *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"

#define MODULE_NAME OpenTransport
#include <base/api-module.h>


namespace Executor {

namespace ot { uint32_t init_priv(const Call &); }OT_FUNCTION(OTClientUtilLib, InitOpenTransportPriv, "OTClnt$LclUtils", "InitOpenTransportPriv", false, "LL", 'L', init_priv);


namespace ot { uint32_t close_priv(const Call &); }OT_FUNCTION(OTClientUtilLib, CloseOpenTransportPriv, "OTClnt$LclUtils", "CloseOpenTransportPriv", false, "L", 'V', close_priv);


namespace ot { uint32_t register_priv(const Call &); }OT_FUNCTION(OTClientUtilLib, OTRegisterAsClientPriv, "OTClnt$LclUtils", "OTRegisterAsClientPriv", false, "LLL", 'L', register_priv);


namespace ot { uint32_t unregister_priv(const Call &); }OT_FUNCTION(OTClientUtilLib, OTUnregisterAsClientPriv, "OTClnt$LclUtils", "OTUnregisterAsClientPriv", false, "L", 'L', unregister_priv);


namespace ot { uint32_t alloc_mem_priv(const Call &); }OT_FUNCTION(OTClientUtilLib, OTAllocMemPriv, "OTClnt$LclUtils", "OTAllocMemPriv", false, "LL", 'L', alloc_mem_priv);


namespace ot { uint32_t free_mem_priv(const Call &); }OT_FUNCTION(OTClientUtilLib, OTFreeMemPriv, "OTClnt$LclUtils", "OTFreeMemPriv", false, "LL", 'V', free_mem_priv);


namespace ot { uint32_t open_priv(const Call &); }OT_FUNCTION(OTInetClientLib, OTOpenInternetServicesPriv, "ot:inet$LclInetClnt", "OTOpenInternetServicesPriv", false, "LLLL", 'L', open_priv);


namespace ot { uint32_t async_open_priv(const Call &); }OT_FUNCTION(OTInetClientLib, OTAsyncOpenInternetServicesPriv, "ot:inet$LclInetClnt", "OTAsyncOpenInternetServicesPriv", false, "LLLLL", 'L', async_open_priv);


namespace ot { uint32_t string_to_address(const Call &); }OT_FUNCTION(OTInetClientLib, OTInetStringToAddress, "ot:inet$LclInetClnt", "OTINETSTRINGTOADDRESS", true, "LLL", 'L', string_to_address);


namespace ot { uint32_t address_to_name(const Call &); }OT_FUNCTION(OTInetClientLib, OTInetAddressToName, "ot:inet$LclInetClnt", "OTINETADDRESSTONAME", true, "LLL", 'L', address_to_name);


namespace ot { uint32_t init_inet_address(const Call &); }OT_FUNCTION(OTInetClientLib, OTInitInetAddress, "ot:inet$LclInetClnt", "OTINITINETADDRESS", true, "LWL", 'V', init_inet_address);


namespace ot { uint32_t init_dns_address(const Call &); }OT_FUNCTION(OTInetClientLib, OTInitDNSAddress, "ot:inet$LclInetClnt", "OTINITDNSADDRESS", true, "LL", 'L', init_dns_address);


namespace ot { uint32_t string_to_host(const Call &); }OT_FUNCTION(OTInetClientLib, OTInetStringToHost, "ot:inet$LclInetClnt", "OTINETSTRINGTOHOST", true, "LL", 'L', string_to_host);


namespace ot { uint32_t host_to_string(const Call &); }OT_FUNCTION(OTInetClientLib, OTInetHostToString, "ot:inet$LclInetClnt", "OTINETHOSTTOSTRING", true, "LL", 'V', host_to_string);


namespace ot { uint32_t get_interface_info(const Call &); }OT_FUNCTION(OTInetClientLib, OTInetGetInterfaceInfo, "ot:inet$LclInetClnt", "OTINETGETINTERFACEINFO", true, "LL", 'L', get_interface_info);


namespace ot { uint32_t sys_info(const Call &); }OT_FUNCTION(OTInetClientLib, OTInetSysInfo, "ot:inet$LclInetClnt", "OTINETSYSINFO", true, "LLL", 'L', sys_info);


namespace ot { uint32_t mail_exchange(const Call &); }OT_FUNCTION(OTInetClientLib, OTInetMailExchange, "ot:inet$LclInetClnt", "OTINETMAILEXCHANGE", true, "LLLL", 'L', mail_exchange);


namespace ot { uint32_t query(const Call &); }OT_FUNCTION(OTInetClientLib, OTInetQuery, "ot:inet$LclInetClnt", "OTINETQUERY", true, "LLWWLLLLL", 'L', query);


namespace ot { uint32_t open_endpoint_priv(const Call &); }OT_FUNCTION(OTClientLib, OTOpenEndpointPriv, "OTClnt$LclClnt", "OTOpenEndpointPriv", false, "LLLLL", 'L', open_endpoint_priv);


namespace ot { uint32_t async_open_endpoint_priv(const Call &); }OT_FUNCTION(OTClientLib, OTAsyncOpenEndpointPriv, "OTClnt$LclClnt", "OTAsyncOpenEndpointPriv", false, "LLLLLL", 'L', async_open_endpoint_priv);


namespace ot { uint32_t close_provider_priv(const Call &); }OT_FUNCTION(OTClientLib, OTCloseProviderPriv, "OTClnt$LclClnt", "OTCloseProviderPriv", false, "LL", 'L', close_provider_priv);


namespace ot { uint32_t set_async(const Call &); }OT_FUNCTION(OTClientLib, OTSetAsynchronous, "OTClnt$LclClnt", "OTSETASYNCHRONOUS", true, "L", 'L', set_async);


namespace ot { uint32_t set_sync(const Call &); }OT_FUNCTION(OTClientLib, OTSetSynchronous, "OTClnt$LclClnt", "OTSETSYNCHRONOUS", true, "L", 'L', set_sync);


namespace ot { uint32_t is_sync(const Call &); }OT_FUNCTION(OTClientLib, OTIsSynchronous, "OTClnt$LclClnt", "OTISSYNCHRONOUS", true, "L", 'B', is_sync);


namespace ot { uint32_t set_blocking(const Call &); }OT_FUNCTION(OTClientLib, OTSetBlocking, "OTClnt$LclClnt", "OTSETBLOCKING", true, "L", 'L', set_blocking);


namespace ot { uint32_t set_nonblocking(const Call &); }OT_FUNCTION(OTClientLib, OTSetNonBlocking, "OTClnt$LclClnt", "OTSETNONBLOCKING", true, "L", 'L', set_nonblocking);


namespace ot { uint32_t is_blocking(const Call &); }OT_FUNCTION(OTClientLib, OTIsBlocking, "OTClnt$LclClnt", "OTISBLOCKING", true, "L", 'B', is_blocking);


namespace ot { uint32_t ack_sends(const Call &); }OT_FUNCTION(OTClientLib, OTAckSends, "OTClnt$LclClnt", "OTACKSENDS", true, "L", 'L', ack_sends);


namespace ot { uint32_t dont_ack_sends(const Call &); }OT_FUNCTION(OTClientLib, OTDontAckSends, "OTClnt$LclClnt", "OTDONTACKSENDS", true, "L", 'L', dont_ack_sends);


namespace ot { uint32_t is_acking(const Call &); }OT_FUNCTION(OTClientLib, OTIsAckingSends, "OTClnt$LclClnt", "OTISACKINGSENDS", true, "L", 'B', is_acking);


namespace ot { uint32_t install_notifier(const Call &); }OT_FUNCTION(OTClientLib, OTInstallNotifier, "OTClnt$LclClnt", "OTINSTALLNOTIFIER", true, "LLL", 'L', install_notifier);


namespace ot { uint32_t remove_notifier(const Call &); }OT_FUNCTION(OTClientLib, OTRemoveNotifier, "OTClnt$LclClnt", "OTREMOVENOTIFIER", true, "L", 'V', remove_notifier);


namespace ot { uint32_t get_endpoint_state(const Call &); }OT_FUNCTION(OTClientLib, OTGetEndpointState, "OTClnt$LclClnt", "OTGETENDPOINTSTATE", true, "L", 'L', get_endpoint_state);


namespace ot { uint32_t get_endpoint_info(const Call &); }OT_FUNCTION(OTClientLib, OTGetEndpointInfo, "OTClnt$LclClnt", "OTGETENDPOINTINFO", true, "LL", 'L', get_endpoint_info);


namespace ot { uint32_t look(const Call &); }OT_FUNCTION(OTClientLib, OTLook, "OTClnt$LclClnt", "OTLOOK", true, "L", 'L', look);


namespace ot { uint32_t count_data_bytes(const Call &); }OT_FUNCTION(OTClientLib, OTCountDataBytes, "OTClnt$LclClnt", "OTCOUNTDATABYTES", true, "LL", 'L', count_data_bytes);


namespace ot { uint32_t get_prot_address(const Call &); }OT_FUNCTION(OTClientLib, OTGetProtAddress, "OTClnt$LclClnt", "OTGETPROTADDRESS", true, "LLL", 'L', get_prot_address);


namespace ot { uint32_t ot_bind(const Call &); }OT_FUNCTION(OTClientLib, OTBind, "OTClnt$LclClnt", "OTBIND", true, "LLL", 'L', ot_bind);


namespace ot { uint32_t unbind(const Call &); }OT_FUNCTION(OTClientLib, OTUnbind, "OTClnt$LclClnt", "OTUNBIND", true, "L", 'L', unbind);


namespace ot { uint32_t ot_connect(const Call &); }OT_FUNCTION(OTClientLib, OTConnect, "OTClnt$LclClnt", "OTCONNECT", true, "LLL", 'L', ot_connect);


namespace ot { uint32_t rcv_connect(const Call &); }OT_FUNCTION(OTClientLib, OTRcvConnect, "OTClnt$LclClnt", "OTRCVCONNECT", true, "LL", 'L', rcv_connect);


namespace ot { uint32_t ot_listen(const Call &); }OT_FUNCTION(OTClientLib, OTListen, "OTClnt$LclClnt", "OTLISTEN", true, "LL", 'L', ot_listen);


namespace ot { uint32_t ot_accept(const Call &); }OT_FUNCTION(OTClientLib, OTAccept, "OTClnt$LclClnt", "OTACCEPT", true, "LLL", 'L', ot_accept);


namespace ot { uint32_t snd_disconnect(const Call &); }OT_FUNCTION(OTClientLib, OTSndDisconnect, "OTClnt$LclClnt", "OTSNDDISCONNECT", true, "LL", 'L', snd_disconnect);


namespace ot { uint32_t rcv_disconnect(const Call &); }OT_FUNCTION(OTClientLib, OTRcvDisconnect, "OTClnt$LclClnt", "OTRCVDISCONNECT", true, "LL", 'L', rcv_disconnect);


namespace ot { uint32_t snd_orderly(const Call &); }OT_FUNCTION(OTClientLib, OTSndOrderlyDisconnect, "OTClnt$LclClnt", "OTSNDORDERLYDISCONNECT", true, "L", 'L', snd_orderly);


namespace ot { uint32_t rcv_orderly(const Call &); }OT_FUNCTION(OTClientLib, OTRcvOrderlyDisconnect, "OTClnt$LclClnt", "OTRCVORDERLYDISCONNECT", true, "L", 'L', rcv_orderly);


namespace ot { uint32_t snd(const Call &); }OT_FUNCTION(OTClientLib, OTSnd, "OTClnt$LclClnt", "OTSND", true, "LLLL", 'L', snd);


namespace ot { uint32_t rcv(const Call &); }OT_FUNCTION(OTClientLib, OTRcv, "OTClnt$LclClnt", "OTRCV", true, "LLLL", 'L', rcv);


namespace ot { uint32_t snd_udata(const Call &); }OT_FUNCTION(OTClientLib, OTSndUData, "OTClnt$LclClnt", "OTSNDUDATA", true, "LL", 'L', snd_udata);


namespace ot { uint32_t rcv_udata(const Call &); }OT_FUNCTION(OTClientLib, OTRcvUData, "OTClnt$LclClnt", "OTRCVUDATA", true, "LLL", 'L', rcv_udata);


namespace ot { uint32_t ot_option_management(const Call &); }OT_FUNCTION(OTClientLib, OTOptionManagement, "OTClnt$LclClnt", "OTOPTIONMANAGEMENT", true, "LLL", 'L', ot_option_management);


namespace ot { uint32_t next_option(const Call &); }OT_FUNCTION(OTClientLib, OTNextOption, "OTClnt$LclClnt", "OTNEXTOPTION", true, "LLL", 'L', next_option);


namespace ot { uint32_t find_option(const Call &); }OT_FUNCTION(OTClientLib, OTFindOption, "OTClnt$LclClnt", "OTFINDOPTION", true, "LLLL", 'L', find_option);


namespace ot { uint32_t rcv_uderr(const Call &); }OT_FUNCTION(OTClientLib, OTRcvUDErr, "OTClnt$LclClnt", "OTRCVUDERR", true, "LL", 'L', rcv_uderr);


namespace ot { uint32_t ot_sync(const Call &); }OT_FUNCTION(OTClientLib, OTSync, "OTClnt$LclClnt", "OTSYNC", true, "L", 'L', ot_sync);


namespace ot { uint32_t cancel_sync_calls(const Call &); }OT_FUNCTION(OTClientLib, OTCancelSynchronousCalls, "OTClnt$LclClnt", "OTCANCELSYNCHRONOUSCALLS", true, "LL", 'L', cancel_sync_calls);


namespace ot { uint32_t resolve_address(const Call &); }OT_FUNCTION(OTClientLib, OTResolveAddress, "OTClnt$LclClnt", "OTRESOLVEADDRESS", true, "LLLL", 'L', resolve_address);


namespace ot { uint32_t get_provider_port_ref(const Call &); }OT_FUNCTION(OTClientLib, OTGetProviderPortRef, "OTClnt$LclClnt", "OTGETPROVIDERPORTREF", true, "L", 'L', get_provider_port_ref);


namespace ot { uint32_t ot_ioctl(const Call &); }OT_FUNCTION(OTClientLib, OTIoctl, "OTClnt$LclClnt", "OTIOCTL", true, "LLL", 'L', ot_ioctl);


namespace ot { uint32_t alloc_priv(const Call &); }OT_FUNCTION(OTClientLib, OTAllocPriv, "OTClnt$LclClnt", "OTAllocPriv", false, "LLLLL", 'L', alloc_priv);


namespace ot { uint32_t ot_free(const Call &); }OT_FUNCTION(OTClientLib, OTFree, "OTClnt$LclClnt", "OTFREE", true, "LL", 'L', ot_free);


namespace ot { uint32_t idle(const Call &); }OT_FUNCTION(OTClientLib, OTIdle, "OTClnt$LclClnt", "OTIDLE", true, "", 'V', idle);


namespace ot { uint32_t delay(const Call &); }OT_FUNCTION(OTClientLib, OTDelay, "OTClnt$LclClnt", "OTDELAY", true, "L", 'V', delay);


namespace ot { uint32_t create_deferred(const Call &); }OT_FUNCTION(OTUtilityLib, OTCreateDeferredTaskPriv, "OTClnt$CmnShared", "OTCreateDeferredTaskPriv", false, "LLL", 'L', create_deferred);


namespace ot { uint32_t destroy(const Call &); }OT_FUNCTION(OTUtilityLib, OTDestroyDeferredTask, "OTClnt$CmnShared", "OTDESTROYDEFERREDTASK", true, "L", 'L', destroy);


namespace ot { uint32_t schedule(const Call &); }OT_FUNCTION(OTUtilityLib, OTScheduleDeferredTask, "OTClnt$CmnShared", "OTSCHEDULEDEFERREDTASK", true, "L", 'B', schedule);


namespace ot { uint32_t schedule(const Call &); }OT_FUNCTION(OTUtilityLib, OTScheduleInterruptTask, "OTClnt$CmnShared", "OTSCHEDULEINTERRUPTTASK", true, "L", 'B', schedule);


namespace ot { uint32_t create_system(const Call &); }OT_FUNCTION(OTUtilityLib, OTCreateSystemTaskPriv, "OTClnt$CmnShared", "OTCreateSystemTaskPriv", false, "LLL", 'L', create_system);


namespace ot { uint32_t destroy(const Call &); }OT_FUNCTION(OTUtilityLib, OTDestroySystemTask, "OTClnt$CmnShared", "OTDESTROYSYSTEMTASK", true, "L", 'L', destroy);


namespace ot { uint32_t schedule(const Call &); }OT_FUNCTION(OTUtilityLib, OTScheduleSystemTask, "OTClnt$CmnShared", "OTSCHEDULESYSTEMTASK", true, "L", 'B', schedule);


namespace ot { uint32_t cancel(const Call &); }OT_FUNCTION(OTUtilityLib, OTCancelSystemTask, "OTClnt$CmnShared", "OTCANCELSYSTEMTASK", true, "L", 'B', cancel);


namespace ot { uint32_t can_make_sync_call(const Call &); }OT_FUNCTION(OTUtilityLib, OTCanMakeSyncCall, "OTClnt$CmnShared", "OTCANMAKESYNCCALL", true, "", 'B', can_make_sync_call);


namespace ot { uint32_t is_at_interrupt_level(const Call &); }OT_FUNCTION(OTUtilityLib, OTIsAtInterruptLevel, "OTClnt$CmnShared", "OTIsAtInterruptLevel", false, "", 'B', is_at_interrupt_level);


namespace ot { uint32_t can_load_libraries(const Call &); }OT_FUNCTION(OTUtilityLib, OTCanLoadLibraries, "OTClnt$CmnShared", "OTCanLoadLibraries", false, "", 'B', can_load_libraries);


namespace ot { uint32_t enter_interrupt(const Call &); }OT_FUNCTION(OTUtilityLib, OTEnterInterrupt, "OTClnt$CmnShared", "OTENTERINTERRUPT", true, "", 'V', enter_interrupt);


namespace ot { uint32_t leave_interrupt(const Call &); }OT_FUNCTION(OTUtilityLib, OTLeaveInterrupt, "OTClnt$CmnShared", "OTLEAVEINTERRUPT", true, "", 'V', leave_interrupt);


namespace ot { uint32_t ot_memcpy(const Call &); }OT_FUNCTION(OTUtilityLib, OTMemcpy, "OTClnt$CmnShared", "OTMemcpy", false, "LLL", 'V', ot_memcpy);


namespace ot { uint32_t ot_memcpy(const Call &); }OT_FUNCTION(OTUtilityLib, OTMemmove, "OTClnt$CmnShared", "OTMemmove", false, "LLL", 'V', ot_memcpy);


namespace ot { uint32_t memzero(const Call &); }OT_FUNCTION(OTUtilityLib, OTMemzero, "OTClnt$CmnShared", "OTMemzero", false, "LL", 'V', memzero);


namespace ot { uint32_t ot_memset(const Call &); }OT_FUNCTION(OTUtilityLib, OTMemset, "OTClnt$CmnShared", "OTMemset", false, "LLL", 'V', ot_memset);


namespace ot { uint32_t ot_memcmp(const Call &); }OT_FUNCTION(OTUtilityLib, OTMemcmp, "OTClnt$CmnShared", "OTMemcmp", false, "LLL", 'B', ot_memcmp);


namespace ot { uint32_t strlength(const Call &); }OT_FUNCTION(OTUtilityLib, OTStrLength, "OTClnt$CmnShared", "OTStrLength", false, "L", 'L', strlength);


namespace ot { uint32_t strcopy(const Call &); }OT_FUNCTION(OTUtilityLib, OTStrCopy, "OTClnt$CmnShared", "OTStrCopy", false, "LL", 'V', strcopy);


namespace ot { uint32_t ot_strcat(const Call &); }OT_FUNCTION(OTUtilityLib, OTStrCat, "OTClnt$CmnShared", "OTStrCat", false, "LL", 'V', ot_strcat);


namespace ot { uint32_t strequal(const Call &); }OT_FUNCTION(OTUtilityLib, OTStrEqual, "OTClnt$CmnShared", "OTStrEqual", false, "LL", 'B', strequal);


namespace ot { uint32_t get_time_stamp(const Call &); }OT_FUNCTION(OTUtilityLib, OTGetTimeStamp, "OTClnt$CmnShared", "OTGetTimeStamp", false, "L", 'V', get_time_stamp);


namespace ot { uint32_t subtract_time_stamps(const Call &); }OT_FUNCTION(OTUtilityLib, OTSubtractTimeStamps, "OTClnt$CmnShared", "OTSubtractTimeStamps", false, "LLL", 'L', subtract_time_stamps);


namespace ot { uint32_t stamp_in_ms(const Call &); }OT_FUNCTION(OTUtilityLib, OTTimeStampInMilliseconds, "OTClnt$CmnShared", "OTTimeStampInMilliseconds", false, "L", 'L', stamp_in_ms);


namespace ot { uint32_t stamp_in_us(const Call &); }OT_FUNCTION(OTUtilityLib, OTTimeStampInMicroseconds, "OTClnt$CmnShared", "OTTimeStampInMicroseconds", false, "L", 'L', stamp_in_us);


namespace ot { uint32_t elapsed_ms(const Call &); }OT_FUNCTION(OTUtilityLib, OTElapsedMilliseconds, "OTClnt$CmnShared", "OTElapsedMilliseconds", false, "L", 'L', elapsed_ms);


namespace ot { uint32_t elapsed_us(const Call &); }OT_FUNCTION(OTUtilityLib, OTElapsedMicroseconds, "OTClnt$CmnShared", "OTElapsedMicroseconds", false, "L", 'L', elapsed_us);


namespace ot { uint32_t clock_secs(const Call &); }OT_FUNCTION(OTUtilityLib, OTGetClockTimeInSecs, "OTClnt$CmnShared", "OTGetClockTimeInSecs", false, "", 'L', clock_secs);


namespace ot { uint32_t lifo_enqueue(const Call &); }OT_FUNCTION(OTUtilityLib, OTLIFOEnqueue, "OTClnt$CmnShared", "OTLIFOEnqueue", false, "LL", 'V', lifo_enqueue);


namespace ot { uint32_t lifo_dequeue(const Call &); }OT_FUNCTION(OTUtilityLib, OTLIFODequeue, "OTClnt$CmnShared", "OTLIFODequeue", false, "L", 'L', lifo_dequeue);


namespace ot { uint32_t lifo_steal(const Call &); }OT_FUNCTION(OTUtilityLib, OTLIFOStealList, "OTClnt$CmnShared", "OTLIFOStealList", false, "L", 'L', lifo_steal);


namespace ot { uint32_t reverse_list(const Call &); }OT_FUNCTION(OTUtilityLib, OTReverseList, "OTClnt$CmnShared", "OTReverseList", false, "L", 'L', reverse_list);


namespace ot { uint32_t add_first(const Call &); }OT_FUNCTION(OTUtilityLib, OTAddFirst, "OTClnt$CmnShared", "OTAddFirst", false, "LL", 'V', add_first);


namespace ot { uint32_t add_last(const Call &); }OT_FUNCTION(OTUtilityLib, OTAddLast, "OTClnt$CmnShared", "OTAddLast", false, "LL", 'V', add_last);


namespace ot { uint32_t remove_first(const Call &); }OT_FUNCTION(OTUtilityLib, OTRemoveFirst, "OTClnt$CmnShared", "OTRemoveFirst", false, "L", 'L', remove_first);


namespace ot { uint32_t remove_last(const Call &); }OT_FUNCTION(OTUtilityLib, OTRemoveLast, "OTClnt$CmnShared", "OTRemoveLast", false, "L", 'L', remove_last);


namespace ot { uint32_t get_first(const Call &); }OT_FUNCTION(OTUtilityLib, OTGetFirst, "OTClnt$CmnShared", "OTGetFirst", false, "L", 'L', get_first);


namespace ot { uint32_t get_last(const Call &); }OT_FUNCTION(OTUtilityLib, OTGetLast, "OTClnt$CmnShared", "OTGetLast", false, "L", 'L', get_last);


namespace ot { uint32_t is_in_list(const Call &); }OT_FUNCTION(OTUtilityLib, OTIsInList, "OTClnt$CmnShared", "OTIsInList", false, "LL", 'B', is_in_list);


namespace ot { uint32_t remove_link(const Call &); }OT_FUNCTION(OTUtilityLib, OTRemoveLink, "OTClnt$CmnShared", "OTRemoveLink", false, "LL", 'B', remove_link);


namespace ot { uint32_t get_indexed_link(const Call &); }OT_FUNCTION(OTUtilityLib, OTGetIndexedLink, "OTClnt$CmnShared", "OTGetIndexedLink", false, "LL", 'L', get_indexed_link);


namespace ot { uint32_t enqueue(const Call &); }OT_FUNCTION(OTUtilityLib, OTEnqueue, "OTClnt$CmnShared", "OTEnqueue", false, "LLL", 'V', enqueue);


namespace ot { uint32_t dequeue(const Call &); }OT_FUNCTION(OTUtilityLib, OTDequeue, "OTClnt$CmnShared", "OTDequeue", false, "LL", 'L', dequeue);


namespace ot { uint32_t find_link(const Call &); }OT_FUNCTION(OTUtilityLib, OTFindLink, "OTClnt$CmnShared", "OTFindLink", false, "LLL", 'L', find_link);


namespace ot { uint32_t find_and_remove_link(const Call &); }OT_FUNCTION(OTUtilityLib, OTFindAndRemoveLink, "OTClnt$CmnShared", "OTFindAndRemoveLink", false, "LLL", 'L', find_and_remove_link);


namespace ot { uint32_t atomic_set_bit(const Call &); }OT_FUNCTION(OTUtilityLib, OTAtomicSetBit, "OTClnt$CmnShared", "OTAtomicSetBit", false, "LL", 'B', atomic_set_bit);


namespace ot { uint32_t atomic_clear_bit(const Call &); }OT_FUNCTION(OTUtilityLib, OTAtomicClearBit, "OTClnt$CmnShared", "OTAtomicClearBit", false, "LL", 'B', atomic_clear_bit);


namespace ot { uint32_t atomic_test_bit(const Call &); }OT_FUNCTION(OTUtilityLib, OTAtomicTestBit, "OTClnt$CmnShared", "OTAtomicTestBit", false, "LL", 'B', atomic_test_bit);


namespace ot { uint32_t cas32(const Call &); }OT_FUNCTION(OTUtilityLib, OTCompareAndSwapPtr, "OTClnt$CmnShared", "OTCompareAndSwapPtr", false, "LLL", 'B', cas32);


namespace ot { uint32_t cas32(const Call &); }OT_FUNCTION(OTUtilityLib, OTCompareAndSwap32, "OTClnt$CmnShared", "OTCompareAndSwap32", false, "LLL", 'B', cas32);


namespace ot { uint32_t cas16(const Call &); }OT_FUNCTION(OTUtilityLib, OTCompareAndSwap16, "OTClnt$CmnShared", "OTCompareAndSwap16", false, "LLL", 'B', cas16);


namespace ot { uint32_t cas8(const Call &); }OT_FUNCTION(OTUtilityLib, OTCompareAndSwap8, "OTClnt$CmnShared", "OTCompareAndSwap8", false, "LLL", 'B', cas8);


namespace ot { uint32_t atomic_add32(const Call &); }OT_FUNCTION(OTUtilityLib, OTAtomicAdd32, "OTClnt$CmnShared", "OTAtomicAdd32", false, "LL", 'L', atomic_add32);


namespace ot { uint32_t atomic_add16(const Call &); }OT_FUNCTION(OTUtilityLib, OTAtomicAdd16, "OTClnt$CmnShared", "OTAtomicAdd16", false, "LL", 'L', atomic_add16);


namespace ot { uint32_t atomic_add8(const Call &); }OT_FUNCTION(OTUtilityLib, OTAtomicAdd8, "OTClnt$CmnShared", "OTAtomicAdd8", false, "LL", 'L', atomic_add8);


namespace ot { uint32_t create_port_ref(const Call &); }OT_FUNCTION(OTUtilityLib, OTCreatePortRef, "OTClnt$CmnShared", "OTCREATEPORTREF", true, "WWWW", 'L', create_port_ref);


namespace ot { uint32_t device_type_of(const Call &); }OT_FUNCTION(OTUtilityLib, OTGetDeviceTypeFromPortRef, "OTClnt$CmnShared", "OTGETDEVICETYPEFROMPORTREF", true, "L", 'W', device_type_of);


namespace ot { uint32_t bus_type_of(const Call &); }OT_FUNCTION(OTUtilityLib, OTGetBusTypeFromPortRef, "OTClnt$CmnShared", "OTGETBUSTYPEFROMPORTREF", true, "L", 'W', bus_type_of);


namespace ot { uint32_t slot_of(const Call &); }OT_FUNCTION(OTUtilityLib, OTGetSlotFromPortRef, "OTClnt$CmnShared", "OTGETSLOTFROMPORTREF", true, "LL", 'W', slot_of);


namespace ot { uint32_t get_indexed_port(const Call &); }OT_FUNCTION(OTClientUtilLib, OTGetIndexedPort, "OTClnt$LclUtils", "OTGETINDEXEDPORT", true, "LL", 'B', get_indexed_port);


namespace ot { uint32_t find_port(const Call &); }OT_FUNCTION(OTClientUtilLib, OTFindPort, "OTClnt$LclUtils", "OTFINDPORT", true, "LL", 'B', find_port);


namespace ot { uint32_t find_port_by_ref(const Call &); }OT_FUNCTION(OTClientUtilLib, OTFindPortByRef, "OTClnt$LclUtils", "OTFINDPORTBYREF", true, "LL", 'B', find_port_by_ref);


namespace ot { uint32_t create_configuration(const Call &); }OT_FUNCTION(OTClientUtilLib, OTCreateConfiguration, "OTClnt$LclUtils", "OTCREATECONFIGURATION", true, "L", 'L', create_configuration);


namespace ot { uint32_t clone_configuration(const Call &); }OT_FUNCTION(OTClientUtilLib, OTCloneConfiguration, "OTClnt$LclUtils", "OTCLONECONFIGURATION", true, "L", 'L', clone_configuration);


namespace ot { uint32_t destroy_configuration(const Call &); }OT_FUNCTION(OTClientUtilLib, OTDestroyConfiguration, "OTClnt$LclUtils", "OTDESTROYCONFIGURATION", true, "L", 'V', destroy_configuration);


} /* namespace Executor */