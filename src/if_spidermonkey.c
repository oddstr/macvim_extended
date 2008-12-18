/* vi:set ts=8 sts=4 sw=4:
 */

#include "vim.h"
#include "version.h"

#if !defined(XP_UNIX) && !defined(XP_WIN) && !defined(XP_OS2) \
    && !defined(XP_MAC) && !defined(XP_BEOS)
# if defined(UNIX)
#  define XP_UNIX
# elif defined(MSWIN)
#  define XP_WIN
# elif defined(OS2)
#  define XP_OS2
# elif defined(MACOS)
#  define XP_MAC
# elif defined(__BEOS__)
#  define XP_BEOS
# endif
#endif
#include <jsapi.h>

static int sm_initialized = 0;

static int ensure_sm_initialized(void);
static int sm_init(void);
static void sm_error_reporter(JSContext *cx, const char *message, JSErrorReport *report);
static JSBool sm_branch_callback(JSContext *cx, JSScript *script);

static long range_start;
static long range_end;

static JSRuntime *sm_rt;
static JSContext *sm_cx;
static JSObject  *sm_global;

/*
 * global class
 */
static JSBool sm_print(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval);

static JSFunctionSpec sm_global_methods[] = {
    {"print",	sm_print,	0},
    {0}
};

/*
 * vim class
 */
static JSObject *sm_vim_init(JSContext *cx, JSObject *obj);

static JSBool sm_vim_prop_get(JSContext *cx, JSObject *obj, jsval id, jsval *vp);

static JSBool sm_vim_eval(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval);
static JSBool sm_vim_command(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval);

static JSClass sm_vim_class = {
    "Vim",0,
    0
};

enum sm_vim_tinyid {
    SM_VIM_VERSION,
    SM_VIM_RANGE_START,
    SM_VIM_RANGE_END,
};

static JSPropertySpec sm_vim_props[] = {
    {"version", SM_VIM_VERSION, JSPROP_READONLY, sm_vim_prop_get},
    {"range_start", SM_VIM_RANGE_START, JSPROP_READONLY, sm_vim_prop_get},
    {"range_end", SM_VIM_RANGE_END, JSPROP_READONLY, sm_vim_prop_get},
    {0}
};

static JSFunctionSpec sm_vim_methods[] = {
    {"eval",		sm_vim_eval,		1},
    {"command",		sm_vim_command,		1},
    {0}
};

/*
 * buffers class
 */
static JSObject *sm_buffers_init(JSContext *cx, JSObject *obj);
static JSBool sm_buffers_get(JSContext *cx, JSObject *obj, jsval id, jsval *vp);
static JSBool sm_buffers_enum(JSContext *cx, JSObject *obj, JSIterateOp enum_op, jsval *statep, jsid *idp);

static JSBool sm_buffers_prop_get(JSContext *cx, JSObject *obj, jsval id, jsval *vp);

static buf_T *sm_find_vimbuf(jsval id);

static JSClass sm_buffers_class = {
    "Buffers", JSCLASS_NEW_ENUMERATE,
    0
};

enum sm_buffers_tinyid {
    SM_BUFFERS_COUNT,
    SM_BUFFERS_FIRST,
    SM_BUFFERS_LAST,
    SM_BUFFERS_CURRENT,
};

static JSPropertySpec sm_buffers_props[] = {
    {"count",	SM_BUFFERS_COUNT,   JSPROP_READONLY,	sm_buffers_prop_get},
    {"first",	SM_BUFFERS_FIRST,   JSPROP_READONLY,	sm_buffers_prop_get},
    {"last",	SM_BUFFERS_LAST,    JSPROP_READONLY,	sm_buffers_prop_get},
    {"current",	SM_BUFFERS_CURRENT, JSPROP_READONLY,	sm_buffers_prop_get},
    {0}
};

static JSFunctionSpec sm_buffers_methods[] = {
    {0}
};

/*
 * buffer class
 */
static JSBool sm_buf_del(JSContext *cx, JSObject *obj, jsval id, jsval *vp);
static JSBool sm_buf_get(JSContext *cx, JSObject *obj, jsval id, jsval *vp);
static JSBool sm_buf_set(JSContext *cx, JSObject *obj, jsval id, jsval *vp);
static JSBool sm_buf_enum(JSContext *cx, JSObject *obj, JSIterateOp enum_op, jsval *statep, jsid *idp);
static void sm_buf_finalize(JSContext *cx, JSObject *obj);

static JSBool sm_buf_prop_get(JSContext *cx, JSObject *obj, jsval id, jsval *vp);

static JSBool sm_buf_appendLine(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval);
static JSBool sm_buf_deleteLine(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval);
static JSBool sm_buf_getLine(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval);
static JSBool sm_buf_setLine(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval);

static JSObject *sm_buf_new(JSContext *cx, buf_T *buf);
static buf_T *sm_buf_getptr(JSContext *cx, JSObject *obj);

static void sm_fix_cursor(int lo, int hi, int extra);

static JSClass sm_buf_class = {
    "Buffer", JSCLASS_HAS_PRIVATE | JSCLASS_NEW_ENUMERATE,
    0
};

enum sm_buf_tinyid {
    SM_BUF_LENGTH,
    SM_BUF_NAME,
    SM_BUF_NUMBER,
    SM_BUF_NEXT,
    SM_BUF_PREV,
};

static JSPropertySpec sm_buf_props[] = {
    {"length",	SM_BUF_LENGTH,	JSPROP_READONLY,	sm_buf_prop_get},
    {"name",	SM_BUF_NAME,	JSPROP_READONLY,	sm_buf_prop_get},
    {"number",	SM_BUF_NUMBER,	JSPROP_READONLY,	sm_buf_prop_get},
    {"next",	SM_BUF_NEXT,	JSPROP_READONLY,	sm_buf_prop_get},
    {"prev",	SM_BUF_PREV,	JSPROP_READONLY,	sm_buf_prop_get},
    {0}
};

static JSFunctionSpec sm_buf_methods[] = {
    {"appendLine",	sm_buf_appendLine,	2},
    {"deleteLine",	sm_buf_deleteLine,	1},
    {"getLine",		sm_buf_getLine,		1},
    {"setLine",		sm_buf_setLine,		2},
    {0}
};

/*
 * windows class
 */
static JSObject *sm_windows_init(JSContext *cx, JSObject *obj);
static JSBool sm_windows_get(JSContext *cx, JSObject *obj, jsval id, jsval *vp);
static JSBool sm_windows_enum(JSContext *cx, JSObject *obj, JSIterateOp enum_op, jsval *statep, jsid *idp);

static JSBool sm_windows_prop_get(JSContext *cx, JSObject *obj, jsval id, jsval *vp);

static win_T *sm_find_vimwin(jsval id);

static JSClass sm_windows_class = {
    "Windows", JSCLASS_NEW_ENUMERATE,
    0
};

enum sm_windows_tinyid {
    SM_WINDOWS_COUNT,
    SM_WINDOWS_FIRST,
    SM_WINDOWS_LAST,
    SM_WINDOWS_CURRENT,
};

static JSPropertySpec sm_windows_props[] = {
    {"count",	SM_WINDOWS_COUNT,   JSPROP_READONLY,	sm_windows_prop_get},
    {"first",	SM_WINDOWS_FIRST,   JSPROP_READONLY,	sm_windows_prop_get},
    {"last",	SM_WINDOWS_LAST,    JSPROP_READONLY,	sm_windows_prop_get},
    {"current",	SM_WINDOWS_CURRENT, JSPROP_READONLY,	sm_windows_prop_get},
    {0}
};

static JSFunctionSpec sm_windows_methods[] = {
    {0}
};

/*
 * window class
 */
static void sm_win_finalize(JSContext *cx, JSObject *obj);

static JSBool sm_win_prop_get(JSContext *cx, JSObject *obj, jsval id, jsval *vp);
static JSBool sm_win_prop_set(JSContext *cx, JSObject *obj, jsval id, jsval *vp);

static JSObject *sm_win_new(JSContext *cx, win_T *win);
static win_T *sm_win_getptr(JSContext *cx, JSObject *obj);

static JSClass sm_win_class = {
    "Window", JSCLASS_HAS_PRIVATE,
    0
};

enum sm_win_tinyid {
    SM_WIN_NUMBER,
    SM_WIN_HEIGHT,
    SM_WIN_NEXT,
    SM_WIN_PREV,
    SM_WIN_BUFFER,
};

static JSPropertySpec sm_win_props[] = {
    {"number",	SM_WIN_NUMBER,	JSPROP_READONLY,	sm_win_prop_get},
    {"height",	SM_WIN_HEIGHT,	0, sm_win_prop_get, sm_win_prop_set},
    {"next",	SM_WIN_NEXT,	JSPROP_READONLY,	sm_win_prop_get},
    {"prev",	SM_WIN_PREV,	JSPROP_READONLY,	sm_win_prop_get},
    {"buffer",	SM_WIN_BUFFER,	JSPROP_READONLY,	sm_win_prop_get},
    {0}
};

static JSFunctionSpec sm_win_methods[] = {
    {0}
};

#if defined(DYNAMIC_SPIDERMONKEY) || defined(PROTO)
# ifndef DYNAMIC_SPIDERMONKEY
#  define HINSTANCE int		/* for generating prototypes */
# endif

/*
 * Wrapper defines
 */
#if 0
#define JS_Now dll_JS_Now
#define JS_GetNaNValue dll_JS_GetNaNValue
#define JS_GetNegativeInfinityValue dll_JS_GetNegativeInfinityValue
#define JS_GetPositiveInfinityValue dll_JS_GetPositiveInfinityValue
#define JS_GetEmptyStringValue dll_JS_GetEmptyStringValue
#ifdef va_start
#define JS_ConvertArgumentsVA dll_JS_ConvertArgumentsVA
#endif
#define JS_PushArguments dll_JS_PushArguments
#ifdef va_start
#define JS_PushArgumentsVA dll_JS_PushArgumentsVA
#endif
#define JS_PopArguments dll_JS_PopArguments
#ifdef JS_ARGUMENT_FORMATTER_DEFINED
#define JS_AddArgumentFormatter dll_JS_AddArgumentFormatter
#define JS_RemoveArgumentFormatter dll_JS_RemoveArgumentFormatter
#endif
#define JS_ConvertValue dll_JS_ConvertValue
#define JS_ValueToObject dll_JS_ValueToObject
#define JS_ValueToFunction dll_JS_ValueToFunction
#define JS_ValueToConstructor dll_JS_ValueToConstructor
#define JS_ValueToNumber dll_JS_ValueToNumber
#define JS_ValueToECMAInt32 dll_JS_ValueToECMAInt32
#define JS_ValueToECMAUint32 dll_JS_ValueToECMAUint32
#define JS_ValueToUint16 dll_JS_ValueToUint16
#define JS_ValueToBoolean dll_JS_ValueToBoolean
#define JS_TypeOfValue dll_JS_TypeOfValue
#define JS_GetTypeName dll_JS_GetTypeName
#define JS_ShutDown dll_JS_ShutDown
#define JS_GetRuntimePrivate dll_JS_GetRuntimePrivate
#define JS_SetRuntimePrivate dll_JS_SetRuntimePrivate
#ifdef JS_THREADSAFE
#define JS_BeginRequest dll_JS_BeginRequest
#define JS_EndRequest dll_JS_EndRequest
#define JS_YieldRequest dll_JS_YieldRequest
#define JS_SuspendRequest dll_JS_SuspendRequest
#define JS_ResumeRequest dll_JS_ResumeRequest
#endif
#define JS_Lock dll_JS_Lock
#define JS_Unlock dll_JS_Unlock
#define JS_DestroyContext dll_JS_DestroyContext
#define JS_DestroyContextNoGC dll_JS_DestroyContextNoGC
#define JS_DestroyContextMaybeGC dll_JS_DestroyContextMaybeGC
#define JS_GetContextPrivate dll_JS_GetContextPrivate
#define JS_SetContextPrivate dll_JS_SetContextPrivate
#define JS_GetRuntime dll_JS_GetRuntime
#define JS_ContextIterator dll_JS_ContextIterator
#define JS_GetVersion dll_JS_GetVersion
#define JS_SetVersion dll_JS_SetVersion
#define JS_VersionToString dll_JS_VersionToString
#define JS_StringToVersion dll_JS_StringToVersion
#define JS_GetOptions dll_JS_GetOptions
#define JS_SetOptions dll_JS_SetOptions
#define JS_ToggleOptions dll_JS_ToggleOptions
#define JS_GetImplementationVersion dll_JS_GetImplementationVersion
#define JS_GetGlobalObject dll_JS_GetGlobalObject
#define JS_SetGlobalObject dll_JS_SetGlobalObject
#define JS_ResolveStandardClass dll_JS_ResolveStandardClass
#define JS_EnumerateStandardClasses dll_JS_EnumerateStandardClasses
#define JS_GetScopeChain dll_JS_GetScopeChain
#define JS_malloc dll_JS_malloc
#define JS_realloc dll_JS_realloc
#define JS_free dll_JS_free
#define JS_strdup dll_JS_strdup
#define JS_NewDouble dll_JS_NewDouble
#define JS_NewDoubleValue dll_JS_NewDoubleValue
#define JS_NewNumberValue dll_JS_NewNumberValue
#define JS_AddRoot dll_JS_AddRoot
#define JS_AddNamedRoot dll_JS_AddNamedRoot
#define JS_AddNamedRootRT dll_JS_AddNamedRootRT
#define JS_RemoveRoot dll_JS_RemoveRoot
#define JS_RemoveRootRT dll_JS_RemoveRootRT
#define JS_ClearNewbornRoots dll_JS_ClearNewbornRoots
#define JS_EnterLocalRootScope dll_JS_EnterLocalRootScope
#define JS_LeaveLocalRootScope dll_JS_LeaveLocalRootScope
#define JS_ForgetLocalRoot dll_JS_ForgetLocalRoot
#ifdef JS_____DEBUG
#define JS_DumpNamedRoots dll_JS_DumpNamedRoots
#endif
#define JS_MapGCRoots dll_JS_MapGCRoots
#define JS_LockGCThing dll_JS_LockGCThing
#define JS_LockGCThingRT dll_JS_LockGCThingRT
#define JS_UnlockGCThing dll_JS_UnlockGCThing
#define JS_UnlockGCThingRT dll_JS_UnlockGCThingRT
#define JS_MarkGCThing dll_JS_MarkGCThing
#define JS_GC dll_JS_GC
#define JS_MaybeGC dll_JS_MaybeGC
#define JS_SetGCCallback dll_JS_SetGCCallback
#define JS_SetGCCallbackRT dll_JS_SetGCCallbackRT
#define JS_IsAboutToBeFinalized dll_JS_IsAboutToBeFinalized
#define JS_AddExternalStringFinalizer dll_JS_AddExternalStringFinalizer
#define JS_RemoveExternalStringFinalizer dll_JS_RemoveExternalStringFinalizer
#define JS_NewExternalString dll_JS_NewExternalString
#define JS_GetExternalStringGCType dll_JS_GetExternalStringGCType
#define JS_SetThreadStackLimit dll_JS_SetThreadStackLimit
#define JS_DestroyIdArray dll_JS_DestroyIdArray
#define JS_ValueToId dll_JS_ValueToId
#define JS_IdToValue dll_JS_IdToValue
#define JS_InitClass dll_JS_InitClass
#define JS_GetClass dll_JS_GetClass
#define JS_InstanceOf dll_JS_InstanceOf
#define JS_GetInstancePrivate dll_JS_GetInstancePrivate
#define JS_GetPrototype dll_JS_GetPrototype
#define JS_SetPrototype dll_JS_SetPrototype
#define JS_GetParent dll_JS_GetParent
#define JS_SetParent dll_JS_SetParent
#define JS_GetConstructor dll_JS_GetConstructor
#define JS_GetObjectId dll_JS_GetObjectId
#define JS_SealObject dll_JS_SealObject
#define JS_ConstructObject dll_JS_ConstructObject
#define JS_ConstructObjectWithArguments dll_JS_ConstructObjectWithArguments
#define JS_DefineConstDoubles dll_JS_DefineConstDoubles
#define JS_GetPropertyAttributes dll_JS_GetPropertyAttributes
#define JS_SetPropertyAttributes dll_JS_SetPropertyAttributes
#define JS_DefinePropertyWithTinyId dll_JS_DefinePropertyWithTinyId
#define JS_AliasProperty dll_JS_AliasProperty
#define JS_HasProperty dll_JS_HasProperty
#define JS_LookupProperty dll_JS_LookupProperty
#define JS_LookupPropertyWithFlags dll_JS_LookupPropertyWithFlags
#define JS_GetProperty dll_JS_GetProperty
#define JS_SetProperty dll_JS_SetProperty
#define JS_DeleteProperty dll_JS_DeleteProperty
#define JS_DeleteProperty2 dll_JS_DeleteProperty2
#define JS_DefineUCProperty dll_JS_DefineUCProperty
#define JS_GetUCPropertyAttributes dll_JS_GetUCPropertyAttributes
#define JS_SetUCPropertyAttributes dll_JS_SetUCPropertyAttributes
#define JS_DefineUCPropertyWithTinyId dll_JS_DefineUCPropertyWithTinyId
#define JS_HasUCProperty dll_JS_HasUCProperty
#define JS_LookupUCProperty dll_JS_LookupUCProperty
#define JS_GetUCProperty dll_JS_GetUCProperty
#define JS_SetUCProperty dll_JS_SetUCProperty
#define JS_DeleteUCProperty2 dll_JS_DeleteUCProperty2
#define JS_NewArrayObject dll_JS_NewArrayObject
#define JS_IsArrayObject dll_JS_IsArrayObject
#define JS_GetArrayLength dll_JS_GetArrayLength
#define JS_SetArrayLength dll_JS_SetArrayLength
#define JS_HasArrayLength dll_JS_HasArrayLength
#define JS_DefineElement dll_JS_DefineElement
#define JS_AliasElement dll_JS_AliasElement
#define JS_HasElement dll_JS_HasElement
#define JS_LookupElement dll_JS_LookupElement
#define JS_GetElement dll_JS_GetElement
#define JS_SetElement dll_JS_SetElement
#define JS_DeleteElement dll_JS_DeleteElement
#define JS_DeleteElement2 dll_JS_DeleteElement2
#define JS_ClearScope dll_JS_ClearScope
#define JS_Enumerate dll_JS_Enumerate
#define JS_CheckAccess dll_JS_CheckAccess
#define JS_SetCheckObjectAccessCallback dll_JS_SetCheckObjectAccessCallback
#define JS_GetReservedSlot dll_JS_GetReservedSlot
#define JS_SetReservedSlot dll_JS_SetReservedSlot
#ifdef JS_THREADSAFE
#define JS_HoldPrincipals dll_JS_HoldPrincipals
#define JS_DropPrincipals dll_JS_DropPrincipals
#endif
#define JS_SetPrincipalsTranscoder dll_JS_SetPrincipalsTranscoder
#define JS_SetObjectPrincipalsFinder dll_JS_SetObjectPrincipalsFinder
#define JS_NewFunction dll_JS_NewFunction
#define JS_GetFunctionObject dll_JS_GetFunctionObject
#define JS_GetFunctionName dll_JS_GetFunctionName
#define JS_GetFunctionId dll_JS_GetFunctionId
#define JS_GetFunctionFlags dll_JS_GetFunctionFlags
#define JS_ObjectIsFunction dll_JS_ObjectIsFunction
#define JS_DefineFunction dll_JS_DefineFunction
#define JS_DefineUCFunction dll_JS_DefineUCFunction
#define JS_CloneFunctionObject dll_JS_CloneFunctionObject
#define JS_BufferIsCompilableUnit dll_JS_BufferIsCompilableUnit
#define JS_CompileScriptForPrincipals dll_JS_CompileScriptForPrincipals
#define JS_CompileUCScript dll_JS_CompileUCScript
#define JS_CompileUCScriptForPrincipals dll_JS_CompileUCScriptForPrincipals
#define JS_CompileFileHandle dll_JS_CompileFileHandle
#define JS_CompileFileHandleForPrincipals dll_JS_CompileFileHandleForPrincipals
#define JS_NewScriptObject dll_JS_NewScriptObject
#define JS_GetScriptObject dll_JS_GetScriptObject
#define JS_CompileFunction dll_JS_CompileFunction
#define JS_CompileFunctionForPrincipals dll_JS_CompileFunctionForPrincipals
#define JS_CompileUCFunction dll_JS_CompileUCFunction
#define JS_CompileUCFunctionForPrincipals dll_JS_CompileUCFunctionForPrincipals
#define JS_DecompileScript dll_JS_DecompileScript
#define JS_DecompileFunction dll_JS_DecompileFunction
#define JS_DecompileFunctionBody dll_JS_DecompileFunctionBody
#define JS_ExecuteScriptPart dll_JS_ExecuteScriptPart
#define JS_EvaluateScript dll_JS_EvaluateScript
#define JS_EvaluateScriptForPrincipals dll_JS_EvaluateScriptForPrincipals
#define JS_EvaluateUCScript dll_JS_EvaluateUCScript
#define JS_EvaluateUCScriptForPrincipals dll_JS_EvaluateUCScriptForPrincipals
#define JS_CallFunction dll_JS_CallFunction
#define JS_CallFunctionName dll_JS_CallFunctionName
#define JS_CallFunctionValue dll_JS_CallFunctionValue
#define JS_IsRunning dll_JS_IsRunning
#define JS_IsConstructing dll_JS_IsConstructing
#define JS_SetCallReturnValue2 dll_JS_SetCallReturnValue2
#define JS_NewStringCopyN dll_JS_NewStringCopyN
#define JS_InternString dll_JS_InternString
#define JS_NewUCString dll_JS_NewUCString
#define JS_NewUCStringCopyN dll_JS_NewUCStringCopyN
#define JS_NewUCStringCopyZ dll_JS_NewUCStringCopyZ
#define JS_InternUCStringN dll_JS_InternUCStringN
#define JS_InternUCString dll_JS_InternUCString
#define JS_GetStringChars dll_JS_GetStringChars
#define JS_GetStringLength dll_JS_GetStringLength
#define JS_CompareStrings dll_JS_CompareStrings
#define JS_NewGrowableString dll_JS_NewGrowableString
#define JS_NewDependentString dll_JS_NewDependentString
#define JS_ConcatStrings dll_JS_ConcatStrings
#define JS_UndependString dll_JS_UndependString
#define JS_MakeStringImmutable dll_JS_MakeStringImmutable
#define JS_SetLocaleCallbacks dll_JS_SetLocaleCallbacks
#define JS_GetLocaleCallbacks dll_JS_GetLocaleCallbacks
#define JS_ReportErrorNumber dll_JS_ReportErrorNumber
#define JS_ReportErrorNumberUC dll_JS_ReportErrorNumberUC
#define JS_ReportWarning dll_JS_ReportWarning
#define JS_ReportErrorFlagsAndNumber dll_JS_ReportErrorFlagsAndNumber
#define JS_ReportErrorFlagsAndNumberUC dll_JS_ReportErrorFlagsAndNumberUC
#define JS_ReportOutOfMemory dll_JS_ReportOutOfMemory
#define JS_NewRegExpObject dll_JS_NewRegExpObject
#define JS_NewUCRegExpObject dll_JS_NewUCRegExpObject
#define JS_SetRegExpInput dll_JS_SetRegExpInput
#define JS_ClearRegExpStatics dll_JS_ClearRegExpStatics
#define JS_ClearRegExpRoots dll_JS_ClearRegExpRoots
#define JS_IsExceptionPending dll_JS_IsExceptionPending
#define JS_GetPendingException dll_JS_GetPendingException
#define JS_SetPendingException dll_JS_SetPendingException
#define JS_ReportPendingException dll_JS_ReportPendingException
#define JS_SaveExceptionState dll_JS_SaveExceptionState
#define JS_RestoreExceptionState dll_JS_RestoreExceptionState
#define JS_DropExceptionState dll_JS_DropExceptionState
#define JS_ErrorFromException dll_JS_ErrorFromException
#ifdef JS_THREADSAFE
#define JS_GetContextThread dll_JS_GetContextThread
#define JS_SetContextThread dll_JS_SetContextThread
#define JS_ClearContextThread dll_JS_ClearContextThread
#endif
#endif /* if 0 */
#define JS_SetPrivate dll_JS_SetPrivate
#define JS_ValueToString dll_JS_ValueToString
#define JS_GetStringBytes dll_JS_GetStringBytes
#define JS_PropertyStub dll_JS_PropertyStub
#define JS_EnumerateStub dll_JS_EnumerateStub
#define JS_ResolveStub dll_JS_ResolveStub
#define JS_ConvertStub dll_JS_ConvertStub
#define JS_FinalizeStub dll_JS_FinalizeStub
#ifndef JS_NewRuntime
# define JS_NewRuntime dll_JS_NewRuntime
#else
# define JS_Init dll_JS_NewRuntime
#endif
#ifndef JS_DestroyRuntime
# define JS_DestroyRuntime dll_JS_DestroyRuntime
#else
# define JS_Finish dll_JS_DestroyRuntime
#endif
#define JS_NewContext dll_JS_NewContext
#define JS_NewObject dll_JS_NewObject
#define JS_InitStandardClasses dll_JS_InitStandardClasses
#define JS_DefineFunctions dll_JS_DefineFunctions
#define JS_DefineObject dll_JS_DefineObject
#define JS_DefineProperty dll_JS_DefineProperty
#define JS_DefineProperties dll_JS_DefineProperties
#define JS_SetBranchCallback dll_JS_SetBranchCallback
#define JS_ClearPendingException dll_JS_ClearPendingException
#define JS_CompileScript dll_JS_CompileScript
#define JS_CompileFile dll_JS_CompileFile
#define JS_ExecuteScript dll_JS_ExecuteScript
#define JS_DestroyScript dll_JS_DestroyScript
#define JS_NewStringCopyZ dll_JS_NewStringCopyZ
#define JS_SetErrorReporter dll_JS_SetErrorReporter
#define JS_ConvertArguments dll_JS_ConvertArguments
#define JS_GetPrivate dll_JS_GetPrivate
#define JS_ReportError dll_JS_ReportError
#define JS_NewString dll_JS_NewString
#define JS_ValueToInt32 dll_JS_ValueToInt32

/*
 * Pointers for dynamic link
 */
#if 0
static int64(*dll_JS_Now)();
static jsval(*dll_JS_GetNaNValue)(JSContext *cx);
static jsval(*dll_JS_GetNegativeInfinityValue)(JSContext *cx);
static jsval(*dll_JS_GetPositiveInfinityValue)(JSContext *cx);
static jsval(*dll_JS_GetEmptyStringValue)(JSContext *cx);
#ifdef va_start
static JSBool(*dll_JS_ConvertArgumentsVA)(JSContext *cx, uintN argc, jsval *argv, const char *format, va_list ap);
#endif
static jsval *(*dll_JS_PushArguments)(JSContext *cx, void **markp, const char *format, ...);
#ifdef va_start
static jsval *(*dll_JS_PushArgumentsVA)(JSContext *cx, void **markp, const char *format, va_list ap);
#endif
static void(*dll_JS_PopArguments)(JSContext *cx, void *mark);
#ifdef JS_ARGUMENT_FORMATTER_DEFINED
static JSBool(*dll_JS_AddArgumentFormatter)(JSContext *cx, const char *format, JSArgumentFormatter formatter);
static void(*dll_JS_RemoveArgumentFormatter)(JSContext *cx, const char *format);
#endif
static JSBool(*dll_JS_ConvertValue)(JSContext *cx, jsval v, JSType type, jsval *vp);
static JSBool(*dll_JS_ValueToObject)(JSContext *cx, jsval v, JSObject **objp);
static JSFunction *(*dll_JS_ValueToFunction)(JSContext *cx, jsval v);
static JSFunction *(*dll_JS_ValueToConstructor)(JSContext *cx, jsval v);
static JSBool(*dll_JS_ValueToNumber)(JSContext *cx, jsval v, jsdouble *dp);
static JSBool(*dll_JS_ValueToECMAInt32)(JSContext *cx, jsval v, int32 *ip);
static JSBool(*dll_JS_ValueToECMAUint32)(JSContext *cx, jsval v, uint32 *ip);
static JSBool(*dll_JS_ValueToUint16)(JSContext *cx, jsval v, uint16 *ip);
static JSBool(*dll_JS_ValueToBoolean)(JSContext *cx, jsval v, JSBool *bp);
static JSType(*dll_JS_TypeOfValue)(JSContext *cx, jsval v);
static const char *(*dll_JS_GetTypeName)(JSContext *cx, JSType type);
static void(*dll_JS_ShutDown)(void);
static void *(*dll_JS_GetRuntimePrivate)(JSRuntime *rt);
static void(*dll_JS_SetRuntimePrivate)(JSRuntime *rt, void *data);
#ifdef JS_THREADSAFE
static void(*dll_JS_BeginRequest)(JSContext *cx);
static void(*dll_JS_EndRequest)(JSContext *cx);
static void(*dll_JS_YieldRequest)(JSContext *cx);
static jsrefcount(*dll_JS_SuspendRequest)(JSContext *cx);
static void(*dll_JS_ResumeRequest)(JSContext *cx, jsrefcount saveDepth);
#endif
static void(*dll_JS_Lock)(JSRuntime *rt);
static void(*dll_JS_Unlock)(JSRuntime *rt);
static void(*dll_JS_DestroyContext)(JSContext *cx);
static void(*dll_JS_DestroyContextNoGC)(JSContext *cx);
static void(*dll_JS_DestroyContextMaybeGC)(JSContext *cx);
static void *(*dll_JS_GetContextPrivate)(JSContext *cx);
static void(*dll_JS_SetContextPrivate)(JSContext *cx, void *data);
static JSRuntime *(*dll_JS_GetRuntime)(JSContext *cx);
static JSContext *(*dll_JS_ContextIterator)(JSRuntime *rt, JSContext **iterp);
static JSVersion(*dll_JS_GetVersion)(JSContext *cx);
static JSVersion(*dll_JS_SetVersion)(JSContext *cx, JSVersion version);
static const char *(*dll_JS_VersionToString)(JSVersion version);
static JSVersion(*dll_JS_StringToVersion)(const char *string);
static uint32(*dll_JS_GetOptions)(JSContext *cx);
static uint32(*dll_JS_SetOptions)(JSContext *cx, uint32 options);
static uint32(*dll_JS_ToggleOptions)(JSContext *cx, uint32 options);
static const char *(*dll_JS_GetImplementationVersion)(void);
static JSObject *(*dll_JS_GetGlobalObject)(JSContext *cx);
static void(*dll_JS_SetGlobalObject)(JSContext *cx, JSObject *obj);
static JSBool(*dll_JS_ResolveStandardClass)(JSContext *cx, JSObject *obj, jsval id, JSBool *resolved);
static JSBool(*dll_JS_EnumerateStandardClasses)(JSContext *cx, JSObject *obj);
static JSObject *(*dll_JS_GetScopeChain)(JSContext *cx);
static void *(*dll_JS_malloc)(JSContext *cx, size_t nbytes);
static void *(*dll_JS_realloc)(JSContext *cx, void *p, size_t nbytes);
static void(*dll_JS_free)(JSContext *cx, void *p);
static char *(*dll_JS_strdup)(JSContext *cx, const char *s);
static jsdouble *(*dll_JS_NewDouble)(JSContext *cx, jsdouble d);
static JSBool(*dll_JS_NewDoubleValue)(JSContext *cx, jsdouble d, jsval *rval);
static JSBool(*dll_JS_NewNumberValue)(JSContext *cx, jsdouble d, jsval *rval);
static JSBool(*dll_JS_AddRoot)(JSContext *cx, void *rp);
static JSBool(*dll_JS_AddNamedRoot)(JSContext *cx, void *rp, const char *name);
static JSBool(*dll_JS_AddNamedRootRT)(JSRuntime *rt, void *rp, const char *name);
static JSBool(*dll_JS_RemoveRoot)(JSContext *cx, void *rp);
static JSBool(*dll_JS_RemoveRootRT)(JSRuntime *rt, void *rp);
static void(*dll_JS_ClearNewbornRoots)(JSContext *cx);
static JSBool(*dll_JS_EnterLocalRootScope)(JSContext *cx);
static void(*dll_JS_LeaveLocalRootScope)(JSContext *cx);
static void(*dll_JS_ForgetLocalRoot)(JSContext *cx, void *thing);
#ifdef JS_____DEBUG
static void JS_DumpNamedRoots(JSRuntime *rt, void (*dump)(const char *name, void *rp, void *data), void *data);
#endif
static uint32(*dll_JS_MapGCRoots)(JSRuntime *rt, JSGCRootMapFun map, void *data);
static JSBool(*dll_JS_LockGCThing)(JSContext *cx, void *thing);
static JSBool(*dll_JS_LockGCThingRT)(JSRuntime *rt, void *thing);
static JSBool(*dll_JS_UnlockGCThing)(JSContext *cx, void *thing);
static JSBool(*dll_JS_UnlockGCThingRT)(JSRuntime *rt, void *thing);
static void(*dll_JS_MarkGCThing)(JSContext *cx, void *thing, const char *name, void *arg);
static void(*dll_JS_GC)(JSContext *cx);
static void(*dll_JS_MaybeGC)(JSContext *cx);
static JSGCCallback(*dll_JS_SetGCCallback)(JSContext *cx, JSGCCallback cb);
static JSGCCallback(*dll_JS_SetGCCallbackRT)(JSRuntime *rt, JSGCCallback cb);
static JSBool(*dll_JS_IsAboutToBeFinalized)(JSContext *cx, void *thing);
static intN(*dll_JS_AddExternalStringFinalizer)(JSStringFinalizeOp finalizer);
static intN(*dll_JS_RemoveExternalStringFinalizer)(JSStringFinalizeOp finalizer);
static JSString *(*dll_JS_NewExternalString)(JSContext *cx, jschar *chars, size_t length, intN type);
static intN(*dll_JS_GetExternalStringGCType)(JSRuntime *rt, JSString *str);
static void(*dll_JS_SetThreadStackLimit)(JSContext *cx, jsuword limitAddr);
static void(*dll_JS_DestroyIdArray)(JSContext *cx, JSIdArray *ida);
static JSBool(*dll_JS_ValueToId)(JSContext *cx, jsval v, jsid *idp);
static JSBool(*dll_JS_IdToValue)(JSContext *cx, jsid id, jsval *vp);
static JSObject *(*dll_JS_InitClass)(JSContext *cx, JSObject *obj, JSObject *parent_proto, JSClass *clasp, JSNative constructor, uintN nargs, JSPropertySpec *ps, JSFunctionSpec *fs, JSPropertySpec *static_ps, JSFunctionSpec *static_fs);
#ifdef JS_THREADSAFE
static JSClass *(*dll_JS_GetClass)(JSContext *cx, JSObject *obj);
#else
static JSClass *(*dll_JS_GetClass)(JSObject *obj);
#endif
static JSBool(*dll_JS_InstanceOf)(JSContext *cx, JSObject *obj, JSClass *clasp, jsval *argv);
static void *(*dll_JS_GetInstancePrivate)(JSContext *cx, JSObject *obj, JSClass *clasp, jsval *argv);
static JSObject *(*dll_JS_GetPrototype)(JSContext *cx, JSObject *obj);
static JSBool(*dll_JS_SetPrototype)(JSContext *cx, JSObject *obj, JSObject *proto);
static JSObject *(*dll_JS_GetParent)(JSContext *cx, JSObject *obj);
static JSBool(*dll_JS_SetParent)(JSContext *cx, JSObject *obj, JSObject *parent);
static JSObject *(*dll_JS_GetConstructor)(JSContext *cx, JSObject *proto);
static JSBool(*dll_JS_GetObjectId)(JSContext *cx, JSObject *obj, jsid *idp);
static JSBool(*dll_JS_SealObject)(JSContext *cx, JSObject *obj, JSBool deep);
static JSObject *(*dll_JS_ConstructObject)(JSContext *cx, JSClass *clasp, JSObject *proto, JSObject *parent);
static JSObject *(*dll_JS_ConstructObjectWithArguments)(JSContext *cx, JSClass *clasp, JSObject *proto, JSObject *parent, uintN argc, jsval *argv);
static JSBool(*dll_JS_DefineConstDoubles)(JSContext *cx, JSObject *obj, JSConstDoubleSpec *cds);
static JSBool(*dll_JS_GetPropertyAttributes)(JSContext *cx, JSObject *obj, const char *name, uintN *attrsp, JSBool *foundp);
static JSBool(*dll_JS_SetPropertyAttributes)(JSContext *cx, JSObject *obj, const char *name, uintN attrs, JSBool *foundp);
static JSBool(*dll_JS_DefinePropertyWithTinyId)(JSContext *cx, JSObject *obj, const char *name, int8 tinyid, jsval value, JSPropertyOp getter, JSPropertyOp setter, uintN attrs);
static JSBool(*dll_JS_AliasProperty)(JSContext *cx, JSObject *obj, const char *name, const char *alias);
static JSBool(*dll_JS_HasProperty)(JSContext *cx, JSObject *obj, const char *name, JSBool *foundp);
static JSBool(*dll_JS_LookupProperty)(JSContext *cx, JSObject *obj, const char *name, jsval *vp);
static JSBool(*dll_JS_LookupPropertyWithFlags)(JSContext *cx, JSObject *obj, const char *name, uintN flags, jsval *vp);
static JSBool(*dll_JS_GetProperty)(JSContext *cx, JSObject *obj, const char *name, jsval *vp);
static JSBool(*dll_JS_SetProperty)(JSContext *cx, JSObject *obj, const char *name, jsval *vp);
static JSBool(*dll_JS_DeleteProperty)(JSContext *cx, JSObject *obj, const char *name);
static JSBool(*dll_JS_DeleteProperty2)(JSContext *cx, JSObject *obj, const char *name, jsval *rval);
static JSBool(*dll_JS_DefineUCProperty)(JSContext *cx, JSObject *obj, const jschar *name, size_t namelen, jsval value, JSPropertyOp getter, JSPropertyOp setter, uintN attrs);
static JSBool(*dll_JS_GetUCPropertyAttributes)(JSContext *cx, JSObject *obj, const jschar *name, size_t namelen, uintN *attrsp, JSBool *foundp);
static JSBool(*dll_JS_SetUCPropertyAttributes)(JSContext *cx, JSObject *obj, const jschar *name, size_t namelen, uintN attrs, JSBool *foundp);
static JSBool(*dll_JS_DefineUCPropertyWithTinyId)(JSContext *cx, JSObject *obj, const jschar *name, size_t namelen, int8 tinyid, jsval value, JSPropertyOp getter, JSPropertyOp setter, uintN attrs);
static JSBool(*dll_JS_HasUCProperty)(JSContext *cx, JSObject *obj, const jschar *name, size_t namelen, JSBool *vp);
static JSBool(*dll_JS_LookupUCProperty)(JSContext *cx, JSObject *obj, const jschar *name, size_t namelen, jsval *vp);
static JSBool(*dll_JS_GetUCProperty)(JSContext *cx, JSObject *obj, const jschar *name, size_t namelen, jsval *vp);
static JSBool(*dll_JS_SetUCProperty)(JSContext *cx, JSObject *obj, const jschar *name, size_t namelen, jsval *vp);
static JSBool(*dll_JS_DeleteUCProperty2)(JSContext *cx, JSObject *obj, const jschar *name, size_t namelen, jsval *rval);
static JSObject *(*dll_JS_NewArrayObject)(JSContext *cx, jsint length, jsval *vector);
static JSBool(*dll_JS_IsArrayObject)(JSContext *cx, JSObject *obj);
static JSBool(*dll_JS_GetArrayLength)(JSContext *cx, JSObject *obj, jsuint *lengthp);
static JSBool(*dll_JS_SetArrayLength)(JSContext *cx, JSObject *obj, jsuint length);
static JSBool(*dll_JS_HasArrayLength)(JSContext *cx, JSObject *obj, jsuint *lengthp);
static JSBool(*dll_JS_DefineElement)(JSContext *cx, JSObject *obj, jsint index, jsval value, JSPropertyOp getter, JSPropertyOp setter, uintN attrs);
static JSBool(*dll_JS_AliasElement)(JSContext *cx, JSObject *obj, const char *name, jsint alias);
static JSBool(*dll_JS_HasElement)(JSContext *cx, JSObject *obj, jsint index, JSBool *foundp);
static JSBool(*dll_JS_LookupElement)(JSContext *cx, JSObject *obj, jsint index, jsval *vp);
static JSBool(*dll_JS_GetElement)(JSContext *cx, JSObject *obj, jsint index, jsval *vp);
static JSBool(*dll_JS_SetElement)(JSContext *cx, JSObject *obj, jsint index, jsval *vp);
static JSBool(*dll_JS_DeleteElement)(JSContext *cx, JSObject *obj, jsint index);
static JSBool(*dll_JS_DeleteElement2)(JSContext *cx, JSObject *obj, jsint index, jsval *rval);
static void(*dll_JS_ClearScope)(JSContext *cx, JSObject *obj);
static JSIdArray *(*dll_JS_Enumerate)(JSContext *cx, JSObject *obj);
static JSBool(*dll_JS_CheckAccess)(JSContext *cx, JSObject *obj, jsid id, JSAccessMode mode, jsval *vp, uintN *attrsp);
static JSCheckAccessOp(*dll_JS_SetCheckObjectAccessCallback)(JSRuntime *rt, JSCheckAccessOp acb);
static JSBool(*dll_JS_GetReservedSlot)(JSContext *cx, JSObject *obj, uint32 index, jsval *vp);
static JSBool(*dll_JS_SetReservedSlot)(JSContext *cx, JSObject *obj, uint32 index, jsval v);
#ifdef JS_THREADSAFE
static jsrefcount(*dll_JS_HoldPrincipals)(JSContext *cx, JSPrincipals *principals);
static jsrefcount(*dll_JS_DropPrincipals)(JSContext *cx, JSPrincipals *principals);
#endif
static JSPrincipalsTranscoder(*dll_JS_SetPrincipalsTranscoder)(JSRuntime *rt, JSPrincipalsTranscoder px);
static JSObjectPrincipalsFinder(*dll_JS_SetObjectPrincipalsFinder)(JSContext *cx, JSObjectPrincipalsFinder fop);
static JSFunction *(*dll_JS_NewFunction)(JSContext *cx, JSNative call, uintN nargs, uintN flags, JSObject *parent, const char *name);
static JSObject *(*dll_JS_GetFunctionObject)(JSFunction *fun);
static const char *(*dll_JS_GetFunctionName)(JSFunction *fun);
static JSString *(*dll_JS_GetFunctionId)(JSFunction *fun);
static uintN(*dll_JS_GetFunctionFlags)(JSFunction *fun);
static JSBool(*dll_JS_ObjectIsFunction)(JSContext *cx, JSObject *obj);
static JSFunction *(*dll_JS_DefineFunction)(JSContext *cx, JSObject *obj, const char *name, JSNative call, uintN nargs, uintN attrs);
static JSFunction *(*dll_JS_DefineUCFunction)(JSContext *cx, JSObject *obj, const jschar *name, size_t namelen, JSNative call, uintN nargs, uintN attrs);
static JSObject *(*dll_JS_CloneFunctionObject)(JSContext *cx, JSObject *funobj, JSObject *parent);
static JSBool(*dll_JS_BufferIsCompilableUnit)(JSContext *cx, JSObject *obj, const char *bytes, size_t length);
static JSScript *(*dll_JS_CompileScriptForPrincipals)(JSContext *cx, JSObject *obj, JSPrincipals *principals, const char *bytes, size_t length, const char *filename, uintN lineno);
static JSScript *(*dll_JS_CompileUCScript)(JSContext *cx, JSObject *obj, const jschar *chars, size_t length, const char *filename, uintN lineno);
static JSScript *(*dll_JS_CompileUCScriptForPrincipals)(JSContext *cx, JSObject *obj, JSPrincipals *principals, const jschar *chars, size_t length, const char *filename, uintN lineno);
static JSScript *(*dll_JS_CompileFileHandle)(JSContext *cx, JSObject *obj, const char *filename, FILE *fh);
static JSScript *(*dll_JS_CompileFileHandleForPrincipals)(JSContext *cx, JSObject *obj, const char *filename, FILE *fh, JSPrincipals *principals);
static JSObject *(*dll_JS_NewScriptObject)(JSContext *cx, JSScript *script);
static JSObject *(*dll_JS_GetScriptObject)(JSScript *script);
static JSFunction *(*dll_JS_CompileFunction)(JSContext *cx, JSObject *obj, const char *name, uintN nargs, const char **argnames, const char *bytes, size_t length, const char *filename, uintN lineno);
static JSFunction *(*dll_JS_CompileFunctionForPrincipals)(JSContext *cx, JSObject *obj, JSPrincipals *principals, const char *name, uintN nargs, const char **argnames, const char *bytes, size_t length, const char *filename, uintN lineno);
static JSFunction *(*dll_JS_CompileUCFunction)(JSContext *cx, JSObject *obj, const char *name, uintN nargs, const char **argnames, const jschar *chars, size_t length, const char *filename, uintN lineno);
static JSFunction *(*dll_JS_CompileUCFunctionForPrincipals)(JSContext *cx, JSObject *obj, JSPrincipals *principals, const char *name, uintN nargs, const char **argnames, const jschar *chars, size_t length, const char *filename, uintN lineno);
static JSString *(*dll_JS_DecompileScript)(JSContext *cx, JSScript *script, const char *name, uintN indent);
static JSString *(*dll_JS_DecompileFunction)(JSContext *cx, JSFunction *fun, uintN indent);
static JSString *(*dll_JS_DecompileFunctionBody)(JSContext *cx, JSFunction *fun, uintN indent);
static JSBool(*dll_JS_ExecuteScriptPart)(JSContext *cx, JSObject *obj, JSScript *script, JSExecPart part, jsval *rval);
static JSBool(*dll_JS_EvaluateScript)(JSContext *cx, JSObject *obj, const char *bytes, uintN length, const char *filename, uintN lineno, jsval *rval);
static JSBool(*dll_JS_EvaluateScriptForPrincipals)(JSContext *cx, JSObject *obj, JSPrincipals *principals, const char *bytes, uintN length, const char *filename, uintN lineno, jsval *rval);
static JSBool(*dll_JS_EvaluateUCScript)(JSContext *cx, JSObject *obj, const jschar *chars, uintN length, const char *filename, uintN lineno, jsval *rval);
static JSBool(*dll_JS_EvaluateUCScriptForPrincipals)(JSContext *cx, JSObject *obj, JSPrincipals *principals, const jschar *chars, uintN length, const char *filename, uintN lineno, jsval *rval);
static JSBool(*dll_JS_CallFunction)(JSContext *cx, JSObject *obj, JSFunction *fun, uintN argc, jsval *argv, jsval *rval);
static JSBool(*dll_JS_CallFunctionName)(JSContext *cx, JSObject *obj, const char *name, uintN argc, jsval *argv, jsval *rval);
static JSBool(*dll_JS_CallFunctionValue)(JSContext *cx, JSObject *obj, jsval fval, uintN argc, jsval *argv, jsval *rval);
static JSBool(*dll_JS_IsRunning)(JSContext *cx);
static JSBool(*dll_JS_IsConstructing)(JSContext *cx);
static void(*dll_JS_SetCallReturnValue2)(JSContext *cx, jsval v);
static JSString *(*dll_JS_NewStringCopyN)(JSContext *cx, const char *s, size_t n);
static JSString *(*dll_JS_InternString)(JSContext *cx, const char *s);
static JSString *(*dll_JS_NewUCString)(JSContext *cx, jschar *chars, size_t length);
static JSString *(*dll_JS_NewUCStringCopyN)(JSContext *cx, const jschar *s, size_t n);
static JSString *(*dll_JS_NewUCStringCopyZ)(JSContext *cx, const jschar *s);
static JSString *(*dll_JS_InternUCStringN)(JSContext *cx, const jschar *s, size_t length);
static JSString *(*dll_JS_InternUCString)(JSContext *cx, const jschar *s);
static jschar *(*dll_JS_GetStringChars)(JSString *str);
static size_t(*dll_JS_GetStringLength)(JSString *str);
static intN(*dll_JS_CompareStrings)(JSString *str1, JSString *str2);
static JSString *(*dll_JS_NewGrowableString)(JSContext *cx, jschar *chars, size_t length);
static JSString *(*dll_JS_NewDependentString)(JSContext *cx, JSString *str, size_t start, size_t length);
static JSString *(*dll_JS_ConcatStrings)(JSContext *cx, JSString *left, JSString *right);
static const jschar *(*dll_JS_UndependString)(JSContext *cx, JSString *str);
static JSBool(*dll_JS_MakeStringImmutable)(JSContext *cx, JSString *str);
static void(*dll_JS_SetLocaleCallbacks)(JSContext *cx, JSLocaleCallbacks *callbacks);
static JSLocaleCallbacks *(*dll_JS_GetLocaleCallbacks)(JSContext *cx);
static void(*dll_JS_ReportErrorNumber)(JSContext *cx, JSErrorCallback errorCallback, void *userRef, const uintN errorNumber, ...);
static void(*dll_JS_ReportErrorNumberUC)(JSContext *cx, JSErrorCallback errorCallback, void *userRef, const uintN errorNumber, ...);
static JSBool(*dll_JS_ReportWarning)(JSContext *cx, const char *format, ...);
static JSBool(*dll_JS_ReportErrorFlagsAndNumber)(JSContext *cx, uintN flags, JSErrorCallback errorCallback, void *userRef, const uintN errorNumber, ...);
static JSBool(*dll_JS_ReportErrorFlagsAndNumberUC)(JSContext *cx, uintN flags, JSErrorCallback errorCallback, void *userRef, const uintN errorNumber, ...);
static void(*dll_JS_ReportOutOfMemory)(JSContext *cx);
static JSObject *(*dll_JS_NewRegExpObject)(JSContext *cx, char *bytes, size_t length, uintN flags);
static JSObject *(*dll_JS_NewUCRegExpObject)(JSContext *cx, jschar *chars, size_t length, uintN flags);
static void(*dll_JS_SetRegExpInput)(JSContext *cx, JSString *input, JSBool multiline);
static void(*dll_JS_ClearRegExpStatics)(JSContext *cx);
static void(*dll_JS_ClearRegExpRoots)(JSContext *cx);
static JSBool(*dll_JS_IsExceptionPending)(JSContext *cx);
static JSBool(*dll_JS_GetPendingException)(JSContext *cx, jsval *vp);
static void(*dll_JS_SetPendingException)(JSContext *cx, jsval v);
static JSBool(*dll_JS_ReportPendingException)(JSContext *cx);
static JSExceptionState *(*dll_JS_SaveExceptionState)(JSContext *cx);
static void(*dll_JS_RestoreExceptionState)(JSContext *cx, JSExceptionState *state);
static void(*dll_JS_DropExceptionState)(JSContext *cx, JSExceptionState *state);
static JSErrorReport *(*dll_JS_ErrorFromException)(JSContext *cx, jsval v);
#ifdef JS_THREADSAFE
static jsword(*dll_JS_GetContextThread)(JSContext *cx);
static jsword(*dll_JS_SetContextThread)(JSContext *cx);
static jsword(*dll_JS_ClearContextThread)(JSContext *cx);
#endif
#endif /* if 0 */
static JSBool(*dll_JS_SetPrivate)(JSContext *cx, JSObject *obj, void *data);
static JSString *(*dll_JS_ValueToString)(JSContext *cx, jsval v);
static char *(*dll_JS_GetStringBytes)(JSString *str);
static JSBool(*dll_JS_PropertyStub)(JSContext *cx, JSObject *obj, jsval id, jsval *vp);
static JSBool(*dll_JS_EnumerateStub)(JSContext *cx, JSObject *obj);
static JSBool(*dll_JS_ResolveStub)(JSContext *cx, JSObject *obj, jsval id);
static JSBool(*dll_JS_ConvertStub)(JSContext *cx, JSObject *obj, JSType type, jsval *vp);
static void(*dll_JS_FinalizeStub)(JSContext *cx, JSObject *obj);
static JSRuntime *(*dll_JS_NewRuntime)(uint32 maxbytes);
static void(*dll_JS_DestroyRuntime)(JSRuntime *rt);
static JSContext *(*dll_JS_NewContext)(JSRuntime *rt, size_t stackChunkSize);
static JSObject *(*dll_JS_NewObject)(JSContext *cx, JSClass *clasp, JSObject *proto, JSObject *parent);
static JSBool(*dll_JS_InitStandardClasses)(JSContext *cx, JSObject *obj);
static JSBool(*dll_JS_DefineFunctions)(JSContext *cx, JSObject *obj, JSFunctionSpec *fs);
static JSObject *(*dll_JS_DefineObject)(JSContext *cx, JSObject *obj, const char *name, JSClass *clasp, JSObject *proto, uintN attrs);
static JSBool(*dll_JS_DefineProperty)(JSContext *cx, JSObject *obj, const char *name, jsval value, JSPropertyOp getter, JSPropertyOp setter, uintN attrs);
static JSBool(*dll_JS_DefineProperties)(JSContext *cx, JSObject *obj, JSPropertySpec *ps);
static JSBranchCallback(*dll_JS_SetBranchCallback)(JSContext *cx, JSBranchCallback cb);
static void(*dll_JS_ClearPendingException)(JSContext *cx);
static JSScript *(*dll_JS_CompileScript)(JSContext *cx, JSObject *obj, const char *bytes, size_t length, const char *filename, uintN lineno);
static JSScript *(*dll_JS_CompileFile)(JSContext *cx, JSObject *obj, const char *filename);
static JSBool(*dll_JS_ExecuteScript)(JSContext *cx, JSObject *obj, JSScript *script, jsval *rval);
static void(*dll_JS_DestroyScript)(JSContext *cx, JSScript *script);
static JSString *(*dll_JS_NewStringCopyZ)(JSContext *cx, const char *s);
static JSErrorReporter(*dll_JS_SetErrorReporter)(JSContext *cx, JSErrorReporter er);
static JSBool(*dll_JS_ConvertArguments)(JSContext *cx, uintN argc, jsval *argv, const char *format, ...);
static void *(*dll_JS_GetPrivate)(JSContext *cx, JSObject *obj);
static void(*dll_JS_ReportError)(JSContext *cx, const char *format, ...);
static JSString *(*dll_JS_NewString)(JSContext *cx, char *bytes, size_t length);
static JSBool(*dll_JS_ValueToInt32)(JSContext *cx, jsval v, int32 *ip);

static HINSTANCE hinstSpiderMonkey = 0; /* Instance of js.dll */

/*
 * Table of name to function pointer of spidermonkey.
 */
# define SPIDERMONKEY_PROC FARPROC
static struct
{
    char *name;
    SPIDERMONKEY_PROC *ptr;
} sm_funcname_table[] =
{
#if 0
    {"JS_Now", (SPIDERMONKEY_PROC*)&dll_JS_Now},
    {"JS_GetNaNValue", (SPIDERMONKEY_PROC*)&dll_JS_GetNaNValue},
    {"JS_GetNegativeInfinityValue", (SPIDERMONKEY_PROC*)&dll_JS_GetNegativeInfinityValue},
    {"JS_GetPositiveInfinityValue", (SPIDERMONKEY_PROC*)&dll_JS_GetPositiveInfinityValue},
    {"JS_GetEmptyStringValue", (SPIDERMONKEY_PROC*)&dll_JS_GetEmptyStringValue},
#ifdef va_start
    {"JS_ConvertArgumentsVA", (SPIDERMONKEY_PROC*)&dll_JS_ConvertArgumentsVA},
#endif
    {"JS_PushArguments", (SPIDERMONKEY_PROC*)&dll_JS_PushArguments},
#ifdef va_start
    {"JS_PushArgumentsVA", (SPIDERMONKEY_PROC*)&dll_JS_PushArgumentsVA},
#endif
    {"JS_PopArguments", (SPIDERMONKEY_PROC*)&dll_JS_PopArguments},
#ifdef JS_ARGUMENT_FORMATTER_DEFINED
    {"JS_AddArgumentFormatter", (SPIDERMONKEY_PROC*)&dll_JS_AddArgumentFormatter},
    {"JS_RemoveArgumentFormatter", (SPIDERMONKEY_PROC*)&dll_JS_RemoveArgumentFormatter},
#endif
    {"JS_ConvertValue", (SPIDERMONKEY_PROC*)&dll_JS_ConvertValue},
    {"JS_ValueToObject", (SPIDERMONKEY_PROC*)&dll_JS_ValueToObject},
    {"JS_ValueToFunction", (SPIDERMONKEY_PROC*)&dll_JS_ValueToFunction},
    {"JS_ValueToConstructor", (SPIDERMONKEY_PROC*)&dll_JS_ValueToConstructor},
    {"JS_ValueToNumber", (SPIDERMONKEY_PROC*)&dll_JS_ValueToNumber},
    {"JS_ValueToECMAInt32", (SPIDERMONKEY_PROC*)&dll_JS_ValueToECMAInt32},
    {"JS_ValueToECMAUint32", (SPIDERMONKEY_PROC*)&dll_JS_ValueToECMAUint32},
    {"JS_ValueToUint16", (SPIDERMONKEY_PROC*)&dll_JS_ValueToUint16},
    {"JS_ValueToBoolean", (SPIDERMONKEY_PROC*)&dll_JS_ValueToBoolean},
    {"JS_TypeOfValue", (SPIDERMONKEY_PROC*)&dll_JS_TypeOfValue},
    {"JS_GetTypeName", (SPIDERMONKEY_PROC*)&dll_JS_GetTypeName},
    {"JS_ShutDown", (SPIDERMONKEY_PROC*)&dll_JS_ShutDown},
    {"JS_GetRuntimePrivate", (SPIDERMONKEY_PROC*)&dll_JS_GetRuntimePrivate},
    {"JS_SetRuntimePrivate", (SPIDERMONKEY_PROC*)&dll_JS_SetRuntimePrivate},
#ifdef JS_THREADSAFE
    {"JS_BeginRequest", (SPIDERMONKEY_PROC*)&dll_JS_BeginRequest},
    {"JS_EndRequest", (SPIDERMONKEY_PROC*)&dll_JS_EndRequest},
    {"JS_YieldRequest", (SPIDERMONKEY_PROC*)&dll_JS_YieldRequest},
    {"JS_SuspendRequest", (SPIDERMONKEY_PROC*)&dll_JS_SuspendRequest},
    {"JS_ResumeRequest", (SPIDERMONKEY_PROC*)&dll_JS_ResumeRequest},
#endif
    {"JS_Lock", (SPIDERMONKEY_PROC*)&dll_JS_Lock},
    {"JS_Unlock", (SPIDERMONKEY_PROC*)&dll_JS_Unlock},
    {"JS_DestroyContext", (SPIDERMONKEY_PROC*)&dll_JS_DestroyContext},
    {"JS_DestroyContextNoGC", (SPIDERMONKEY_PROC*)&dll_JS_DestroyContextNoGC},
    {"JS_DestroyContextMaybeGC", (SPIDERMONKEY_PROC*)&dll_JS_DestroyContextMaybeGC},
    {"JS_GetContextPrivate", (SPIDERMONKEY_PROC*)&dll_JS_GetContextPrivate},
    {"JS_SetContextPrivate", (SPIDERMONKEY_PROC*)&dll_JS_SetContextPrivate},
    {"JS_GetRuntime", (SPIDERMONKEY_PROC*)&dll_JS_GetRuntime},
    {"JS_ContextIterator", (SPIDERMONKEY_PROC*)&dll_JS_ContextIterator},
    {"JS_GetVersion", (SPIDERMONKEY_PROC*)&dll_JS_GetVersion},
    {"JS_SetVersion", (SPIDERMONKEY_PROC*)&dll_JS_SetVersion},
    {"JS_VersionToString", (SPIDERMONKEY_PROC*)&dll_JS_VersionToString},
    {"JS_StringToVersion", (SPIDERMONKEY_PROC*)&dll_JS_StringToVersion},
    {"JS_GetOptions", (SPIDERMONKEY_PROC*)&dll_JS_GetOptions},
    {"JS_SetOptions", (SPIDERMONKEY_PROC*)&dll_JS_SetOptions},
    {"JS_ToggleOptions", (SPIDERMONKEY_PROC*)&dll_JS_ToggleOptions},
    {"JS_GetImplementationVersion", (SPIDERMONKEY_PROC*)&dll_JS_GetImplementationVersion},
    {"JS_GetGlobalObject", (SPIDERMONKEY_PROC*)&dll_JS_GetGlobalObject},
    {"JS_SetGlobalObject", (SPIDERMONKEY_PROC*)&dll_JS_SetGlobalObject},
    {"JS_ResolveStandardClass", (SPIDERMONKEY_PROC*)&dll_JS_ResolveStandardClass},
    {"JS_EnumerateStandardClasses", (SPIDERMONKEY_PROC*)&dll_JS_EnumerateStandardClasses},
    {"JS_GetScopeChain", (SPIDERMONKEY_PROC*)&dll_JS_GetScopeChain},
    {"JS_malloc", (SPIDERMONKEY_PROC*)&dll_JS_malloc},
    {"JS_realloc", (SPIDERMONKEY_PROC*)&dll_JS_realloc},
    {"JS_free", (SPIDERMONKEY_PROC*)&dll_JS_free},
    {"JS_strdup", (SPIDERMONKEY_PROC*)&dll_JS_strdup},
    {"JS_NewDouble", (SPIDERMONKEY_PROC*)&dll_JS_NewDouble},
    {"JS_NewDoubleValue", (SPIDERMONKEY_PROC*)&dll_JS_NewDoubleValue},
    {"JS_NewNumberValue", (SPIDERMONKEY_PROC*)&dll_JS_NewNumberValue},
    {"JS_AddRoot", (SPIDERMONKEY_PROC*)&dll_JS_AddRoot},
    {"JS_AddNamedRoot", (SPIDERMONKEY_PROC*)&dll_JS_AddNamedRoot},
    {"JS_AddNamedRootRT", (SPIDERMONKEY_PROC*)&dll_JS_AddNamedRootRT},
    {"JS_RemoveRoot", (SPIDERMONKEY_PROC*)&dll_JS_RemoveRoot},
    {"JS_RemoveRootRT", (SPIDERMONKEY_PROC*)&dll_JS_RemoveRootRT},
    {"JS_ClearNewbornRoots", (SPIDERMONKEY_PROC*)&dll_JS_ClearNewbornRoots},
    {"JS_EnterLocalRootScope", (SPIDERMONKEY_PROC*)&dll_JS_EnterLocalRootScope},
    {"JS_LeaveLocalRootScope", (SPIDERMONKEY_PROC*)&dll_JS_LeaveLocalRootScope},
    {"JS_ForgetLocalRoot", (SPIDERMONKEY_PROC*)&dll_JS_ForgetLocalRoot},
#ifdef JS_____DEBUG
    {"JS_DumpNamedRoots", (SPIDERMONKEY_PROC*)&dll_JS_DumpNamedRoots},
#endif
    {"JS_MapGCRoots", (SPIDERMONKEY_PROC*)&dll_JS_MapGCRoots},
    {"JS_LockGCThing", (SPIDERMONKEY_PROC*)&dll_JS_LockGCThing},
    {"JS_LockGCThingRT", (SPIDERMONKEY_PROC*)&dll_JS_LockGCThingRT},
    {"JS_UnlockGCThing", (SPIDERMONKEY_PROC*)&dll_JS_UnlockGCThing},
    {"JS_UnlockGCThingRT", (SPIDERMONKEY_PROC*)&dll_JS_UnlockGCThingRT},
    {"JS_MarkGCThing", (SPIDERMONKEY_PROC*)&dll_JS_MarkGCThing},
    {"JS_GC", (SPIDERMONKEY_PROC*)&dll_JS_GC},
    {"JS_MaybeGC", (SPIDERMONKEY_PROC*)&dll_JS_MaybeGC},
    {"JS_SetGCCallback", (SPIDERMONKEY_PROC*)&dll_JS_SetGCCallback},
    {"JS_SetGCCallbackRT", (SPIDERMONKEY_PROC*)&dll_JS_SetGCCallbackRT},
    {"JS_IsAboutToBeFinalized", (SPIDERMONKEY_PROC*)&dll_JS_IsAboutToBeFinalized},
    {"JS_AddExternalStringFinalizer", (SPIDERMONKEY_PROC*)&dll_JS_AddExternalStringFinalizer},
    {"JS_RemoveExternalStringFinalizer", (SPIDERMONKEY_PROC*)&dll_JS_RemoveExternalStringFinalizer},
    {"JS_NewExternalString", (SPIDERMONKEY_PROC*)&dll_JS_NewExternalString},
    {"JS_GetExternalStringGCType", (SPIDERMONKEY_PROC*)&dll_JS_GetExternalStringGCType},
    {"JS_SetThreadStackLimit", (SPIDERMONKEY_PROC*)&dll_JS_SetThreadStackLimit},
    {"JS_DestroyIdArray", (SPIDERMONKEY_PROC*)&dll_JS_DestroyIdArray},
    {"JS_ValueToId", (SPIDERMONKEY_PROC*)&dll_JS_ValueToId},
    {"JS_IdToValue", (SPIDERMONKEY_PROC*)&dll_JS_IdToValue},
    {"JS_InitClass", (SPIDERMONKEY_PROC*)&dll_JS_InitClass},
    {"JS_GetClass", (SPIDERMONKEY_PROC*)&dll_JS_GetClass},
    {"JS_InstanceOf", (SPIDERMONKEY_PROC*)&dll_JS_InstanceOf},
    {"JS_GetInstancePrivate", (SPIDERMONKEY_PROC*)&dll_JS_GetInstancePrivate},
    {"JS_GetPrototype", (SPIDERMONKEY_PROC*)&dll_JS_GetPrototype},
    {"JS_SetPrototype", (SPIDERMONKEY_PROC*)&dll_JS_SetPrototype},
    {"JS_GetParent", (SPIDERMONKEY_PROC*)&dll_JS_GetParent},
    {"JS_SetParent", (SPIDERMONKEY_PROC*)&dll_JS_SetParent},
    {"JS_GetConstructor", (SPIDERMONKEY_PROC*)&dll_JS_GetConstructor},
    {"JS_GetObjectId", (SPIDERMONKEY_PROC*)&dll_JS_GetObjectId},
    {"JS_SealObject", (SPIDERMONKEY_PROC*)&dll_JS_SealObject},
    {"JS_ConstructObject", (SPIDERMONKEY_PROC*)&dll_JS_ConstructObject},
    {"JS_ConstructObjectWithArguments", (SPIDERMONKEY_PROC*)&dll_JS_ConstructObjectWithArguments},
    {"JS_DefineConstDoubles", (SPIDERMONKEY_PROC*)&dll_JS_DefineConstDoubles},
    {"JS_GetPropertyAttributes", (SPIDERMONKEY_PROC*)&dll_JS_GetPropertyAttributes},
    {"JS_SetPropertyAttributes", (SPIDERMONKEY_PROC*)&dll_JS_SetPropertyAttributes},
    {"JS_DefinePropertyWithTinyId", (SPIDERMONKEY_PROC*)&dll_JS_DefinePropertyWithTinyId},
    {"JS_AliasProperty", (SPIDERMONKEY_PROC*)&dll_JS_AliasProperty},
    {"JS_HasProperty", (SPIDERMONKEY_PROC*)&dll_JS_HasProperty},
    {"JS_LookupProperty", (SPIDERMONKEY_PROC*)&dll_JS_LookupProperty},
    {"JS_LookupPropertyWithFlags", (SPIDERMONKEY_PROC*)&dll_JS_LookupPropertyWithFlags},
    {"JS_GetProperty", (SPIDERMONKEY_PROC*)&dll_JS_GetProperty},
    {"JS_SetProperty", (SPIDERMONKEY_PROC*)&dll_JS_SetProperty},
    {"JS_DeleteProperty", (SPIDERMONKEY_PROC*)&dll_JS_DeleteProperty},
    {"JS_DeleteProperty2", (SPIDERMONKEY_PROC*)&dll_JS_DeleteProperty2},
    {"JS_DefineUCProperty", (SPIDERMONKEY_PROC*)&dll_JS_DefineUCProperty},
    {"JS_GetUCPropertyAttributes", (SPIDERMONKEY_PROC*)&dll_JS_GetUCPropertyAttributes},
    {"JS_SetUCPropertyAttributes", (SPIDERMONKEY_PROC*)&dll_JS_SetUCPropertyAttributes},
    {"JS_DefineUCPropertyWithTinyId", (SPIDERMONKEY_PROC*)&dll_JS_DefineUCPropertyWithTinyId},
    {"JS_HasUCProperty", (SPIDERMONKEY_PROC*)&dll_JS_HasUCProperty},
    {"JS_LookupUCProperty", (SPIDERMONKEY_PROC*)&dll_JS_LookupUCProperty},
    {"JS_GetUCProperty", (SPIDERMONKEY_PROC*)&dll_JS_GetUCProperty},
    {"JS_SetUCProperty", (SPIDERMONKEY_PROC*)&dll_JS_SetUCProperty},
    {"JS_DeleteUCProperty2", (SPIDERMONKEY_PROC*)&dll_JS_DeleteUCProperty2},
    {"JS_NewArrayObject", (SPIDERMONKEY_PROC*)&dll_JS_NewArrayObject},
    {"JS_IsArrayObject", (SPIDERMONKEY_PROC*)&dll_JS_IsArrayObject},
    {"JS_GetArrayLength", (SPIDERMONKEY_PROC*)&dll_JS_GetArrayLength},
    {"JS_SetArrayLength", (SPIDERMONKEY_PROC*)&dll_JS_SetArrayLength},
    {"JS_HasArrayLength", (SPIDERMONKEY_PROC*)&dll_JS_HasArrayLength},
    {"JS_DefineElement", (SPIDERMONKEY_PROC*)&dll_JS_DefineElement},
    {"JS_AliasElement", (SPIDERMONKEY_PROC*)&dll_JS_AliasElement},
    {"JS_HasElement", (SPIDERMONKEY_PROC*)&dll_JS_HasElement},
    {"JS_LookupElement", (SPIDERMONKEY_PROC*)&dll_JS_LookupElement},
    {"JS_GetElement", (SPIDERMONKEY_PROC*)&dll_JS_GetElement},
    {"JS_SetElement", (SPIDERMONKEY_PROC*)&dll_JS_SetElement},
    {"JS_DeleteElement", (SPIDERMONKEY_PROC*)&dll_JS_DeleteElement},
    {"JS_DeleteElement2", (SPIDERMONKEY_PROC*)&dll_JS_DeleteElement2},
    {"JS_ClearScope", (SPIDERMONKEY_PROC*)&dll_JS_ClearScope},
    {"JS_Enumerate", (SPIDERMONKEY_PROC*)&dll_JS_Enumerate},
    {"JS_CheckAccess", (SPIDERMONKEY_PROC*)&dll_JS_CheckAccess},
    {"JS_SetCheckObjectAccessCallback", (SPIDERMONKEY_PROC*)&dll_JS_SetCheckObjectAccessCallback},
    {"JS_GetReservedSlot", (SPIDERMONKEY_PROC*)&dll_JS_GetReservedSlot},
    {"JS_SetReservedSlot", (SPIDERMONKEY_PROC*)&dll_JS_SetReservedSlot},
#ifdef JS_THREADSAFE
    {"JS_HoldPrincipals", (SPIDERMONKEY_PROC*)&dll_JS_HoldPrincipals},
    {"JS_DropPrincipals", (SPIDERMONKEY_PROC*)&dll_JS_DropPrincipals},
#endif
    {"JS_SetPrincipalsTranscoder", (SPIDERMONKEY_PROC*)&dll_JS_SetPrincipalsTranscoder},
    {"JS_SetObjectPrincipalsFinder", (SPIDERMONKEY_PROC*)&dll_JS_SetObjectPrincipalsFinder},
    {"JS_NewFunction", (SPIDERMONKEY_PROC*)&dll_JS_NewFunction},
    {"JS_GetFunctionObject", (SPIDERMONKEY_PROC*)&dll_JS_GetFunctionObject},
    {"JS_GetFunctionName", (SPIDERMONKEY_PROC*)&dll_JS_GetFunctionName},
    {"JS_GetFunctionId", (SPIDERMONKEY_PROC*)&dll_JS_GetFunctionId},
    {"JS_GetFunctionFlags", (SPIDERMONKEY_PROC*)&dll_JS_GetFunctionFlags},
    {"JS_ObjectIsFunction", (SPIDERMONKEY_PROC*)&dll_JS_ObjectIsFunction},
    {"JS_DefineFunction", (SPIDERMONKEY_PROC*)&dll_JS_DefineFunction},
    {"JS_DefineUCFunction", (SPIDERMONKEY_PROC*)&dll_JS_DefineUCFunction},
    {"JS_CloneFunctionObject", (SPIDERMONKEY_PROC*)&dll_JS_CloneFunctionObject},
    {"JS_BufferIsCompilableUnit", (SPIDERMONKEY_PROC*)&dll_JS_BufferIsCompilableUnit},
    {"JS_CompileScriptForPrincipals", (SPIDERMONKEY_PROC*)&dll_JS_CompileScriptForPrincipals},
    {"JS_CompileUCScript", (SPIDERMONKEY_PROC*)&dll_JS_CompileUCScript},
    {"JS_CompileUCScriptForPrincipals", (SPIDERMONKEY_PROC*)&dll_JS_CompileUCScriptForPrincipals},
    {"JS_CompileFileHandle", (SPIDERMONKEY_PROC*)&dll_JS_CompileFileHandle},
    {"JS_CompileFileHandleForPrincipals", (SPIDERMONKEY_PROC*)&dll_JS_CompileFileHandleForPrincipals},
    {"JS_NewScriptObject", (SPIDERMONKEY_PROC*)&dll_JS_NewScriptObject},
    {"JS_GetScriptObject", (SPIDERMONKEY_PROC*)&dll_JS_GetScriptObject},
    {"JS_CompileFunction", (SPIDERMONKEY_PROC*)&dll_JS_CompileFunction},
    {"JS_CompileFunctionForPrincipals", (SPIDERMONKEY_PROC*)&dll_JS_CompileFunctionForPrincipals},
    {"JS_CompileUCFunction", (SPIDERMONKEY_PROC*)&dll_JS_CompileUCFunction},
    {"JS_CompileUCFunctionForPrincipals", (SPIDERMONKEY_PROC*)&dll_JS_CompileUCFunctionForPrincipals},
    {"JS_DecompileScript", (SPIDERMONKEY_PROC*)&dll_JS_DecompileScript},
    {"JS_DecompileFunction", (SPIDERMONKEY_PROC*)&dll_JS_DecompileFunction},
    {"JS_DecompileFunctionBody", (SPIDERMONKEY_PROC*)&dll_JS_DecompileFunctionBody},
    {"JS_ExecuteScriptPart", (SPIDERMONKEY_PROC*)&dll_JS_ExecuteScriptPart},
    {"JS_EvaluateScript", (SPIDERMONKEY_PROC*)&dll_JS_EvaluateScript},
    {"JS_EvaluateScriptForPrincipals", (SPIDERMONKEY_PROC*)&dll_JS_EvaluateScriptForPrincipals},
    {"JS_EvaluateUCScript", (SPIDERMONKEY_PROC*)&dll_JS_EvaluateUCScript},
    {"JS_EvaluateUCScriptForPrincipals", (SPIDERMONKEY_PROC*)&dll_JS_EvaluateUCScriptForPrincipals},
    {"JS_CallFunction", (SPIDERMONKEY_PROC*)&dll_JS_CallFunction},
    {"JS_CallFunctionName", (SPIDERMONKEY_PROC*)&dll_JS_CallFunctionName},
    {"JS_CallFunctionValue", (SPIDERMONKEY_PROC*)&dll_JS_CallFunctionValue},
    {"JS_IsRunning", (SPIDERMONKEY_PROC*)&dll_JS_IsRunning},
    {"JS_IsConstructing", (SPIDERMONKEY_PROC*)&dll_JS_IsConstructing},
    {"JS_SetCallReturnValue2", (SPIDERMONKEY_PROC*)&dll_JS_SetCallReturnValue2},
    {"JS_NewStringCopyN", (SPIDERMONKEY_PROC*)&dll_JS_NewStringCopyN},
    {"JS_InternString", (SPIDERMONKEY_PROC*)&dll_JS_InternString},
    {"JS_NewUCString", (SPIDERMONKEY_PROC*)&dll_JS_NewUCString},
    {"JS_NewUCStringCopyN", (SPIDERMONKEY_PROC*)&dll_JS_NewUCStringCopyN},
    {"JS_NewUCStringCopyZ", (SPIDERMONKEY_PROC*)&dll_JS_NewUCStringCopyZ},
    {"JS_InternUCStringN", (SPIDERMONKEY_PROC*)&dll_JS_InternUCStringN},
    {"JS_InternUCString", (SPIDERMONKEY_PROC*)&dll_JS_InternUCString},
    {"JS_GetStringChars", (SPIDERMONKEY_PROC*)&dll_JS_GetStringChars},
    {"JS_GetStringLength", (SPIDERMONKEY_PROC*)&dll_JS_GetStringLength},
    {"JS_CompareStrings", (SPIDERMONKEY_PROC*)&dll_JS_CompareStrings},
    {"JS_NewGrowableString", (SPIDERMONKEY_PROC*)&dll_JS_NewGrowableString},
    {"JS_NewDependentString", (SPIDERMONKEY_PROC*)&dll_JS_NewDependentString},
    {"JS_ConcatStrings", (SPIDERMONKEY_PROC*)&dll_JS_ConcatStrings},
    {"JS_UndependString", (SPIDERMONKEY_PROC*)&dll_JS_UndependString},
    {"JS_MakeStringImmutable", (SPIDERMONKEY_PROC*)&dll_JS_MakeStringImmutable},
    {"JS_SetLocaleCallbacks", (SPIDERMONKEY_PROC*)&dll_JS_SetLocaleCallbacks},
    {"JS_GetLocaleCallbacks", (SPIDERMONKEY_PROC*)&dll_JS_GetLocaleCallbacks},
    {"JS_ReportErrorNumber", (SPIDERMONKEY_PROC*)&dll_JS_ReportErrorNumber},
    {"JS_ReportErrorNumberUC", (SPIDERMONKEY_PROC*)&dll_JS_ReportErrorNumberUC},
    {"JS_ReportWarning", (SPIDERMONKEY_PROC*)&dll_JS_ReportWarning},
    {"JS_ReportErrorFlagsAndNumber", (SPIDERMONKEY_PROC*)&dll_JS_ReportErrorFlagsAndNumber},
    {"JS_ReportErrorFlagsAndNumberUC", (SPIDERMONKEY_PROC*)&dll_JS_ReportErrorFlagsAndNumberUC},
    {"JS_ReportOutOfMemory", (SPIDERMONKEY_PROC*)&dll_JS_ReportOutOfMemory},
    {"JS_NewRegExpObject", (SPIDERMONKEY_PROC*)&dll_JS_NewRegExpObject},
    {"JS_NewUCRegExpObject", (SPIDERMONKEY_PROC*)&dll_JS_NewUCRegExpObject},
    {"JS_SetRegExpInput", (SPIDERMONKEY_PROC*)&dll_JS_SetRegExpInput},
    {"JS_ClearRegExpStatics", (SPIDERMONKEY_PROC*)&dll_JS_ClearRegExpStatics},
    {"JS_ClearRegExpRoots", (SPIDERMONKEY_PROC*)&dll_JS_ClearRegExpRoots},
    {"JS_IsExceptionPending", (SPIDERMONKEY_PROC*)&dll_JS_IsExceptionPending},
    {"JS_GetPendingException", (SPIDERMONKEY_PROC*)&dll_JS_GetPendingException},
    {"JS_SetPendingException", (SPIDERMONKEY_PROC*)&dll_JS_SetPendingException},
    {"JS_ReportPendingException", (SPIDERMONKEY_PROC*)&dll_JS_ReportPendingException},
    {"JS_SaveExceptionState", (SPIDERMONKEY_PROC*)&dll_JS_SaveExceptionState},
    {"JS_RestoreExceptionState", (SPIDERMONKEY_PROC*)&dll_JS_RestoreExceptionState},
    {"JS_DropExceptionState", (SPIDERMONKEY_PROC*)&dll_JS_DropExceptionState},
    {"JS_ErrorFromException", (SPIDERMONKEY_PROC*)&dll_JS_ErrorFromException},
#ifdef JS_THREADSAFE
    {"JS_GetContextThread", (SPIDERMONKEY_PROC*)&dll_JS_GetContextThread},
    {"JS_SetContextThread", (SPIDERMONKEY_PROC*)&dll_JS_SetContextThread},
    {"JS_ClearContextThread", (SPIDERMONKEY_PROC*)&dll_JS_ClearContextThread},
#endif
#endif /* if 0 */
    {"JS_SetPrivate", (SPIDERMONKEY_PROC*)&dll_JS_SetPrivate},
    {"JS_ValueToString", (SPIDERMONKEY_PROC*)&dll_JS_ValueToString},
    {"JS_GetStringBytes", (SPIDERMONKEY_PROC*)&dll_JS_GetStringBytes},
    {"JS_PropertyStub", (SPIDERMONKEY_PROC*)&dll_JS_PropertyStub},
    {"JS_EnumerateStub", (SPIDERMONKEY_PROC*)&dll_JS_EnumerateStub},
    {"JS_ResolveStub", (SPIDERMONKEY_PROC*)&dll_JS_ResolveStub},
    {"JS_ConvertStub", (SPIDERMONKEY_PROC*)&dll_JS_ConvertStub},
    {"JS_FinalizeStub", (SPIDERMONKEY_PROC*)&dll_JS_FinalizeStub},
#ifndef JS_NewRuntime
    {"JS_NewRuntime", (SPIDERMONKEY_PROC*)&dll_JS_NewRuntime},
#else
    {"JS_Init", (SPIDERMONKEY_PROC*)&dll_JS_NewRuntime},
#endif
#ifndef JS_DestroyRuntime
    {"JS_DestroyRuntime", (SPIDERMONKEY_PROC*)&dll_JS_DestroyRuntime},
#else
    {"JS_Finish", (SPIDERMONKEY_PROC*)&dll_JS_DestroyRuntime},
#endif
    {"JS_NewContext", (SPIDERMONKEY_PROC*)&dll_JS_NewContext},
    {"JS_NewObject", (SPIDERMONKEY_PROC*)&dll_JS_NewObject},
    {"JS_InitStandardClasses", (SPIDERMONKEY_PROC*)&dll_JS_InitStandardClasses},
    {"JS_DefineFunctions", (SPIDERMONKEY_PROC*)&dll_JS_DefineFunctions},
    {"JS_DefineObject", (SPIDERMONKEY_PROC*)&dll_JS_DefineObject},
    {"JS_DefineProperty", (SPIDERMONKEY_PROC*)&dll_JS_DefineProperty},
    {"JS_DefineProperties", (SPIDERMONKEY_PROC*)&dll_JS_DefineProperties},
    {"JS_SetBranchCallback", (SPIDERMONKEY_PROC*)&dll_JS_SetBranchCallback},
    {"JS_ClearPendingException", (SPIDERMONKEY_PROC*)&dll_JS_ClearPendingException},
    {"JS_CompileScript", (SPIDERMONKEY_PROC*)&dll_JS_CompileScript},
    {"JS_CompileFile", (SPIDERMONKEY_PROC*)&dll_JS_CompileFile},
    {"JS_ExecuteScript", (SPIDERMONKEY_PROC*)&dll_JS_ExecuteScript},
    {"JS_DestroyScript", (SPIDERMONKEY_PROC*)&dll_JS_DestroyScript},
    {"JS_NewStringCopyZ", (SPIDERMONKEY_PROC*)&dll_JS_NewStringCopyZ},
    {"JS_SetErrorReporter", (SPIDERMONKEY_PROC*)&dll_JS_SetErrorReporter},
    {"JS_ConvertArguments", (SPIDERMONKEY_PROC*)&dll_JS_ConvertArguments},
    {"JS_GetPrivate", (SPIDERMONKEY_PROC*)&dll_JS_GetPrivate},
    {"JS_ReportError", (SPIDERMONKEY_PROC*)&dll_JS_ReportError},
    {"JS_NewString", (SPIDERMONKEY_PROC*)&dll_JS_NewString},
    {"JS_ValueToInt32", (SPIDERMONKEY_PROC*)&dll_JS_ValueToInt32},

    {"", NULL},
};


/*
 * Free js.dll
 */
    static void
end_dynamic_spidermonkey(void)
{
    if (hinstSpiderMonkey)
    {
	FreeLibrary(hinstSpiderMonkey);
	hinstSpiderMonkey = 0;
    }
}


/*
 * Load library and get all pointers.
 * Parameter 'libname' provides name of DLL.
 * Return OK or FAIL.
 */
    static int
spidermonkey_runtime_link_init(char *libname, int verbose)
{
    int i;

    if (hinstSpiderMonkey)
	return OK;
    hinstSpiderMonkey = LoadLibrary(libname);
    if (!hinstSpiderMonkey)
    {
	if (verbose)
	    EMSG2(_(e_loadlib), libname);
	return FAIL;
    }

    for (i = 0; sm_funcname_table[i].ptr; ++i)
    {
	if ((*sm_funcname_table[i].ptr = GetProcAddress(hinstSpiderMonkey,
			sm_funcname_table[i].name)) == NULL)
	{
	    FreeLibrary(hinstSpiderMonkey);
	    hinstSpiderMonkey = 0;
	    if (verbose)
		EMSG2(_(e_loadfunc), sm_funcname_table[i].name);
	    return FAIL;
	}
    }
    return OK;
}

/*
 * If spidermonkey is enabled (there is installed python on Windows system)
 * return TRUE, else FALSE.
 */
    int
spidermonkey_enabled(verbose)
    int		verbose;
{
    return spidermonkey_runtime_link_init(DYNAMIC_SPIDERMONKEY_DLL, verbose)
	== OK;
}
#endif /* defined(DYNAMIC_SPIDERMONKEY) || defined(PROTO) */

    void
spidermonkey_end(void)
{
#ifdef DYNAMIC_SPIDERMONKEY
    end_dynamic_spidermonkey();
#endif
}

    void
ex_spidermonkey(exarg_T *eap)
{
    char *script_buf;
    JSScript *script;
    jsval result;
    JSBool ok;
    JSString *str;

    script_buf = script_get(eap, eap->arg);
    if (!eap->skip && ensure_sm_initialized())
    {
	range_start = eap->line1;
	range_end = eap->line2;
	JS_ClearPendingException(sm_cx);
	if (script_buf == NULL)
	    script = JS_CompileScript(sm_cx, sm_global,
		    (char *)eap->arg, strlen((char *)eap->arg), NULL, 0);
	else
	    script = JS_CompileScript(sm_cx, sm_global,
		    script_buf, strlen(script_buf), NULL, 0);
	if (script)
	{
	    ok = JS_ExecuteScript(sm_cx, sm_global, script, &result);
	    if (ok && result != JSVAL_VOID && eap->getline == &getexline)
	    {
		str = JS_ValueToString(sm_cx, result);
		if (str)
		    MSG(JS_GetStringBytes(str));
	    }
	    JS_DestroyScript(sm_cx, script);
	}
    }
    vim_free(script_buf);
}

    void
ex_spidermonkeyfile(exarg_T *eap)
{
    JSScript *script;
    jsval result;

    if (ensure_sm_initialized())
    {
	range_start = eap->line1;
	range_end = eap->line2;
	JS_ClearPendingException(sm_cx);
	script = JS_CompileFile(sm_cx, sm_global, (char *)eap->arg);
	if (script)
	{
	    (void)JS_ExecuteScript(sm_cx, sm_global, script, &result);
	    JS_DestroyScript(sm_cx, script);
	}
    }
}

    void
spidermonkey_buffer_free(buf_T *buf)
{
    if (buf->b_spidermonkey_ref)
    {
	JS_SetPrivate(sm_cx, (JSObject *)buf->b_spidermonkey_ref, NULL);
    }
}

#if defined(FEAT_WINDOWS) || defined(PROTO)
    void
spidermonkey_window_free(win_T *win)
{
    if (win->w_spidermonkey_ref)
    {
	JS_SetPrivate(sm_cx, (JSObject *)win->w_spidermonkey_ref, NULL);
    }
}
#endif

    static int
ensure_sm_initialized(void)
{
    if (!sm_initialized)
    {
#ifdef DYNAMIC_SPIDERMONKEY
	if (!spidermonkey_enabled(TRUE))
	{
	    EMSG(_("E266: Sorry, this command is disabled, the SpiderMonkey library could not be loaded."));
	    return 0;
	}
#endif
	if (!sm_init())
	    return 0;
	sm_initialized = 1;
    }
    return sm_initialized;
}

    static int
sm_init(void)
{
    // set up global JS variables, including global and custom objects
    JSRuntime *rt;
    JSContext *cx;
    JSObject  *global;

    sm_vim_class.addProperty = JS_PropertyStub;
    sm_vim_class.delProperty = JS_PropertyStub;
    sm_vim_class.getProperty = JS_PropertyStub;
    sm_vim_class.setProperty = JS_PropertyStub;
    sm_vim_class.enumerate = JS_EnumerateStub;
    sm_vim_class.resolve = JS_ResolveStub;
    sm_vim_class.convert = JS_ConvertStub;
    sm_vim_class.finalize = JS_FinalizeStub;

    sm_buffers_class.addProperty = JS_PropertyStub;
    sm_buffers_class.delProperty = JS_PropertyStub;
    sm_buffers_class.getProperty = sm_buffers_get;
    sm_buffers_class.setProperty = JS_PropertyStub;
    sm_buffers_class.enumerate = (JSEnumerateOp)sm_buffers_enum;
    sm_buffers_class.resolve = JS_ResolveStub;
    sm_buffers_class.convert = JS_ConvertStub;
    sm_buffers_class.finalize = JS_FinalizeStub;

    sm_buf_class.addProperty = JS_PropertyStub;
    sm_buf_class.delProperty = sm_buf_del;
    sm_buf_class.getProperty = sm_buf_get;
    sm_buf_class.setProperty = sm_buf_set;
    sm_buf_class.enumerate = (JSEnumerateOp)sm_buf_enum;
    sm_buf_class.resolve = JS_ResolveStub;
    sm_buf_class.convert = JS_ConvertStub;
    sm_buf_class.finalize = sm_buf_finalize;

    sm_windows_class.addProperty = JS_PropertyStub;
    sm_windows_class.delProperty = JS_PropertyStub;
    sm_windows_class.getProperty = sm_windows_get;
    sm_windows_class.setProperty = JS_PropertyStub;
    sm_windows_class.enumerate = (JSEnumerateOp)sm_windows_enum;
    sm_windows_class.resolve = JS_ResolveStub;
    sm_windows_class.convert = JS_ConvertStub;
    sm_windows_class.finalize = JS_FinalizeStub;

    sm_win_class.addProperty = JS_PropertyStub;
    sm_win_class.delProperty = JS_PropertyStub;
    sm_win_class.getProperty = JS_PropertyStub;
    sm_win_class.setProperty = JS_PropertyStub;
    sm_win_class.enumerate = JS_EnumerateStub;
    sm_win_class.resolve = JS_ResolveStub;
    sm_win_class.convert = JS_ConvertStub;
    sm_win_class.finalize = sm_win_finalize;

    // initialize the JS run time, and return result in rt
    // if rt does not have a value, end the program here
    if ((rt = JS_NewRuntime(8L * 1024L * 1024L)) == NULL)
	return 0;

    // create a context and associate it with the JS run time
    // if cx does not have a value, end the program here
    if ((cx = JS_NewContext(rt, 8192)) == NULL)
	return 0;

    // create the global object here
    if ((global = JS_NewObject(cx, NULL, NULL, NULL)) == NULL)
	return 0;

    // initialize the built-in JS objects and the global object
    if (!JS_InitStandardClasses(cx, global))
	return 0;

    if (!JS_DefineFunctions(cx, global, sm_global_methods))
	return 0;

    if (!sm_vim_init(cx, global))
	return 0;

    JS_SetErrorReporter(cx, sm_error_reporter);
    JS_SetBranchCallback(cx, sm_branch_callback);
    //JS_SetThreadStackLimit(cx, 0);

    sm_rt = rt;
    sm_cx = cx;
    sm_global = global;

    // Before exiting the application, free the JS run time
    //JS_DestroyContext(cx);
    //JS_DestroyRuntime(rt);
    //JS_ShutDown();

    return 1;
}

    static void
sm_error_reporter(JSContext *cx, const char *message, JSErrorReport *report)
{
#define SBUFLEN 1024
    static char s_buf[SBUFLEN];
    int len = 0;
    char *buf;
    char *p;

    if (!report)
    {
	EMSG(message);
	return;
    }

    if (JSREPORT_IS_WARNING(report->flags))
	len += vim_snprintf(NULL, 0, "warning: ");
    if (report->filename)
	len += vim_snprintf(NULL, 0, "\"%s\": ", report->filename);
    if (report->lineno)
	len += vim_snprintf(NULL, 0, "line %d: ", report->lineno);
    if (report->linebuf)
	len += vim_snprintf(NULL, 0, "col %d: ",
		report->tokenptr - report->linebuf + 1);
    len += vim_snprintf(NULL, 0, "%s", message);
    if (report->linebuf)
	len += vim_snprintf(NULL, 0, ": %s", report->linebuf);

    if (len + 1 < SBUFLEN)
	buf = s_buf;
    else
    {
	buf = alloc(len + 1);
	if (!buf)
	{
	    EMSG(_(e_outofmem));
	    return;
	}
    }
    p = buf;

    if (JSREPORT_IS_WARNING(report->flags))
	p += sprintf(p, "warning: ");
    if (report->filename)
	p += sprintf(p, "\"%s\": ", report->filename);
    if (report->lineno)
	p += sprintf(p, "line %d: ", report->lineno);
    if (report->linebuf)
	p += sprintf(p, "col %d: ", report->tokenptr - report->linebuf + 1);
    p += sprintf(p, "%s", message);
    if (report->linebuf)
	p += sprintf(p, ": %s", report->linebuf);
    EMSG(buf);

    if (buf != s_buf)
	vim_free(buf);
}

    static JSBool
sm_branch_callback(JSContext *cx, JSScript *script)
{
    ui_breakcheck();
    if (got_int)
	return JS_FALSE;
    return JS_TRUE;
}

    static JSBool
sm_print(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
    uintN i;
    JSString *str;

    msg_putchar('\n');
    for (i = 0; i < argc; i++)
    {
	str = JS_ValueToString(cx, argv[i]);
	if (!str)
	    return JS_FALSE;
	if (i != 0)
	    msg_putchar(' ');
	MSG_PUTS(JS_GetStringBytes(str));
    }
    return JS_TRUE;
}

    static JSObject *
sm_vim_init(JSContext *cx, JSObject *obj)
{
    JSObject *vimobj = JS_DefineObject(cx, obj, "vim", &sm_vim_class, NULL, 0);
    if (!vimobj)
	return NULL;
    if (!JS_DefineProperties(cx, vimobj, sm_vim_props))
	return NULL;
    if (!JS_DefineFunctions(cx, vimobj, sm_vim_methods))
	return NULL;
    if (!sm_buffers_init(cx, vimobj))
	return NULL;
    if (!sm_windows_init(cx, vimobj))
	return NULL;
    return vimobj;
}

    static JSBool
sm_vim_prop_get(JSContext *cx, JSObject *obj, jsval id, jsval *vp)
{
    switch (JSVAL_TO_INT(id))
    {
	case SM_VIM_VERSION:
	    *vp = INT_TO_JSVAL(VIM_VERSION_100);
	    return JS_TRUE;
	case SM_VIM_RANGE_START:
	    *vp = INT_TO_JSVAL(range_start);
	    return JS_TRUE;
	case SM_VIM_RANGE_END:
	    *vp = INT_TO_JSVAL(range_end);
	    return JS_TRUE;
	default:
	    return JS_FALSE;
    }
}

    static JSBool
sm_vim_eval(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
#ifdef FEAT_EVAL
    char *expr;
    char *str;
    JSString *jsstr;

    if (!JS_ConvertArguments(cx, argc, argv, "s", &expr))
	return JS_FALSE;
    str = (char *)eval_to_string((char_u *)expr, NULL, TRUE);
    if (!str)
    {
	JS_ReportError(cx, "invalid expression");
	return JS_FALSE;
    }
    jsstr = JS_NewStringCopyZ(cx, str);
    vim_free(str);
    if (!jsstr)
	return JS_FALSE;
    *rval = STRING_TO_JSVAL(jsstr);
    return JS_TRUE;
#else
    JS_ReportError(cx, "expressions disabled at compile time");
    return JS_FALSE;
#endif
}

    static JSBool
sm_vim_command(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
    char *str;
    if (!JS_ConvertArguments(cx, argc, argv, "s", &str))
	return JS_FALSE;
    do_cmdline_cmd((char_u *)str);
    update_screen(VALID);
    return JS_TRUE;
}

    static JSObject *
sm_buffers_init(JSContext *cx, JSObject *obj)
{
    JSObject *buffers = JS_DefineObject(cx, obj, "buffers", &sm_buffers_class, NULL, 0);
    if (!buffers)
	return NULL;
    if (!JS_DefineProperties(cx, buffers, sm_buffers_props))
	return NULL;
    if (!JS_DefineFunctions(cx, buffers, sm_buffers_methods))
	return NULL;
    return buffers;
}

    static JSBool
sm_buffers_get(JSContext *cx, JSObject *obj, jsval id, jsval *vp)
{
    buf_T *buf;
    JSObject *bufobj;

    buf = sm_find_vimbuf(id);
    if (!buf)
	return JS_TRUE; /* return undefined */

    bufobj = sm_buf_new(cx, buf);
    if (!bufobj)
	return JS_FALSE;
    *vp = OBJECT_TO_JSVAL(bufobj);
    return JS_TRUE;
}

    static JSBool
sm_buffers_enum(JSContext *cx, JSObject *obj, JSIterateOp enum_op, jsval *statep, jsid *idp)
{
    switch (enum_op)
    {
	case JSENUMERATE_INIT:
	    *statep = PRIVATE_TO_JSVAL(firstbuf);
	    if (idp)
	    {
		buf_T *buf;
		int n = 0;
		for (buf = firstbuf; buf; buf = buf->b_next)
		    ++n;
		*idp = INT_TO_JSVAL(n);
	    }
	    return JS_TRUE;
	case JSENUMERATE_NEXT:
	    {
		buf_T *buf = (buf_T *)JSVAL_TO_PRIVATE(*statep);
		if (buf)
		{
		    /*
		     * can't enumerate without defined property
		     */
		    if (!JS_DefineProperty(cx, obj, (char *)buf->b_fnum, JSVAL_VOID,
				NULL, NULL, JSPROP_ENUMERATE | JSPROP_INDEX))
			return JS_FALSE;
		    *idp = INT_TO_JSVAL(buf->b_fnum);
		    *statep = PRIVATE_TO_JSVAL(buf->b_next);
		    return JS_TRUE;
		}
	    }
	    /* fall through */
	case JSENUMERATE_DESTROY:
	    *statep = JSVAL_NULL;
	    return JS_TRUE;
	default:
	    return JS_FALSE;
    }
}

    static JSBool
sm_buffers_prop_get(JSContext *cx, JSObject *obj, jsval id, jsval *vp)
{
    switch (JSVAL_TO_INT(id))
    {
	case SM_BUFFERS_COUNT:
	    {
		buf_T *buf;
		int n = 0;
		for (buf = firstbuf; buf; buf = buf->b_next)
		    ++n;
		*vp = INT_TO_JSVAL(n);
	    }
	    return JS_TRUE;
	case SM_BUFFERS_FIRST:
	    {
		JSObject *bufobj = sm_buf_new(cx, firstbuf);
		if (!bufobj)
		    return JS_FALSE;
		*vp = OBJECT_TO_JSVAL(bufobj);
	    }
	    return JS_TRUE;
	case SM_BUFFERS_LAST:
	    {
		JSObject *bufobj = sm_buf_new(cx, lastbuf);
		if (!bufobj)
		    return JS_FALSE;
		*vp = OBJECT_TO_JSVAL(bufobj);
	    }
	    return JS_TRUE;
	case SM_BUFFERS_CURRENT:
	    {
		JSObject *bufobj = sm_buf_new(cx, curbuf);
		if (!bufobj)
		    return JS_FALSE;
		*vp = OBJECT_TO_JSVAL(bufobj);
	    }
	    return JS_TRUE;
	default:
	    return JS_FALSE;
    }
}

    static buf_T *
sm_find_vimbuf(jsval id)
{
    buf_T *buf = NULL;
    if (JSVAL_IS_INT(id))
	buf = buflist_findnr(JSVAL_TO_INT(id));
    else if (JSVAL_IS_STRING(id))
    {
	char *str = JS_GetStringBytes(JSVAL_TO_STRING(id));
	if (strcmp(str, "%") == 0)
	    buf = curbuf;
	else if (strcmp(str, "$") == 0)
	    buf = lastbuf;
	else if (strcmp(str, "#") == 0)
	    buf = buflist_findnr(curwin->w_alt_fnum);
	else
	    buf = buflist_findname((char_u *)str);
    }
    return buf;
}

    static JSBool
sm_buf_del(JSContext *cx, JSObject *obj, jsval id, jsval *vp)
{
    if (JSVAL_IS_INT(id))
    {
	uintN argc = 1;
	jsval argv[1] = {id};
	return sm_buf_deleteLine(cx, obj, argc, argv, vp);
    }
    return JS_TRUE;
}

    static JSBool
sm_buf_get(JSContext *cx, JSObject *obj, jsval id, jsval *vp)
{
    if (JSVAL_IS_INT(id))
    {
	uintN argc = 1;
	jsval argv[1] = {id};
	return sm_buf_getLine(cx, obj, argc, argv, vp);
    }
    return JS_TRUE;
}

    static JSBool
sm_buf_set(JSContext *cx, JSObject *obj, jsval id, jsval *vp)
{
    if (JSVAL_IS_INT(id))
    {
	uintN argc = 2;
	jsval argv[2] = {id, *vp};
	return sm_buf_setLine(cx, obj, argc, argv, vp);
    }
    return JS_TRUE;
}

    static JSBool
sm_buf_enum(JSContext *cx, JSObject *obj, JSIterateOp enum_op, jsval *statep, jsid *idp)
{
    int n;
    buf_T *buf = sm_buf_getptr(cx, obj);
    if (buf == NULL)
	return JS_FALSE;
    switch (enum_op)
    {
	case JSENUMERATE_INIT:
	    *statep = JSVAL_ONE;
	    if (idp)
		*idp = INT_TO_JSVAL(buf->b_ml.ml_line_count);
	    return JS_TRUE;
	case JSENUMERATE_NEXT:
	    n = JSVAL_TO_INT(*statep);
	    if (n <= buf->b_ml.ml_line_count)
	    {
		if (!JS_DefineProperty(cx, obj, (char *)n, JSVAL_VOID,
			    NULL, NULL, JSPROP_ENUMERATE | JSPROP_INDEX))
		    return JS_FALSE;
		*idp = INT_TO_JSVAL(n);
		*statep = INT_TO_JSVAL(n + 1);
		return JS_TRUE;
	    }
	    /* fall through */
	case JSENUMERATE_DESTROY:
	    *statep = JSVAL_NULL;
	    return JS_TRUE;
	default:
	    return JS_FALSE;
    }
}

    static void
sm_buf_finalize(JSContext *cx, JSObject *obj)
{
    buf_T *buf = (buf_T *)JS_GetPrivate(cx, obj);
    if (buf != NULL)
	buf->b_spidermonkey_ref = NULL;
}

    static JSBool
sm_buf_prop_get(JSContext *cx, JSObject *obj, jsval id, jsval *vp)
{
    buf_T *buf = sm_buf_getptr(cx, obj);
    if (buf == NULL)
	return JS_FALSE;
    switch (JSVAL_TO_INT(id))
    {
	case SM_BUF_LENGTH:
	    *vp = INT_TO_JSVAL(buf->b_ml.ml_line_count);
	    return JS_TRUE;
	case SM_BUF_NAME:
	    {
		JSString *str;
		if (buf->b_ffname)
		    str = JS_NewStringCopyZ(cx, (char *)buf->b_ffname);
		else
		    str = JS_NewString(cx, "", 0);
		if (!str)
		    return JS_FALSE;
		*vp = STRING_TO_JSVAL(str);
		return JS_TRUE;
	    }
	case SM_BUF_NUMBER:
	    *vp = INT_TO_JSVAL(buf->b_fnum);
	    return JS_TRUE;
	case SM_BUF_NEXT:
	    if (buf->b_next == NULL)
		*vp = JSVAL_VOID;
	    else
	    {
		JSObject *bufobj = sm_buf_new(cx, buf->b_next);
		if (!bufobj)
		    return JS_FALSE;
		*vp = OBJECT_TO_JSVAL(bufobj);
	    }
	    return JS_TRUE;
	case SM_BUF_PREV:
	    if (buf->b_prev == NULL)
		*vp = JSVAL_VOID;
	    else
	    {
		JSObject *bufobj = sm_buf_new(cx, buf->b_prev);
		if (!bufobj)
		    return JS_FALSE;
		*vp = OBJECT_TO_JSVAL(bufobj);
	    }
	    return JS_TRUE;
	default:
	    return JS_FALSE;
    }
}

    static JSBool
sm_buf_appendLine(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
    buf_T *buf;
    buf_T *savebuf;
    int32 n;
    char *line;

    if (!(buf = sm_buf_getptr(cx, obj)))
	return JS_FALSE;
    if (!JS_ConvertArguments(cx, argc, argv, "js", &n, &line))
	return JS_FALSE;

    if (n < 0 || n > buf->b_ml.ml_line_count)
    {
	JS_ReportError(cx, "index %d out of buffer", n);
	return JS_FALSE;
    }

    savebuf = curbuf;
    curbuf = buf;
    if (u_inssub(n + 1) == FAIL)
	JS_ReportError(cx, "cannot save undo information");
    else if (ml_append((linenr_T)n, (char_u *)line, 0, FALSE) == FAIL)
	JS_ReportError(cx, "cannot insert line");
    else
	appended_lines_mark((linenr_T)n, 1L);
    curbuf = savebuf;
    update_screen(VALID);

    return JS_TRUE;
}

    static JSBool
sm_buf_deleteLine(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
    buf_T *buf;
    buf_T *savebuf;
    int32 n;

    if (!(buf = sm_buf_getptr(cx, obj)))
	return JS_FALSE;
    if (!JS_ConvertArguments(cx, argc, argv, "j", &n))
	return JS_FALSE;

    if (n <= 0 || n > buf->b_ml.ml_line_count)
    {
	JS_ReportError(cx, "index %d out of buffer", n);
	return JS_FALSE;
    }

    savebuf = curbuf;
    curbuf = buf;
    if (u_savedel(n, 1) == FAIL)
	JS_ReportError(cx, "cannot save undo information");
    else if (ml_delete((linenr_T)n, FALSE) == FAIL)
	JS_ReportError(cx, "cannot delete line");
    else
    {
	deleted_lines_mark((linenr_T)n, 1L);
	if (buf == curwin->w_buffer)
	    sm_fix_cursor(n, n + 1, -1);
    }
    curbuf = savebuf;

    return JS_TRUE;
}

    static JSBool
sm_buf_getLine(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
    buf_T *buf;
    int32 n;
    char *line;
    JSString *jsstr;

    if (!(buf = sm_buf_getptr(cx, obj)))
	return JS_FALSE;
    if (!JS_ConvertArguments(cx, argc, argv, "j", &n))
	return JS_FALSE;

    if (n <= 0 || n > buf->b_ml.ml_line_count)
    {
	JS_ReportError(cx, "index %d out of buffer", n);
	return JS_FALSE;
    }

    line = ml_get_buf(buf, n, FALSE);
    jsstr = JS_NewStringCopyZ(cx, line);
    if (!jsstr)
	return JS_FALSE;
    *rval = STRING_TO_JSVAL(jsstr);

    return JS_TRUE;
}

    static JSBool
sm_buf_setLine(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
    buf_T *buf;
    buf_T *savebuf;
    int32 n;
    char *line;

    if (!(buf = sm_buf_getptr(cx, obj)))
	return JS_FALSE;
    if (!JS_ConvertArguments(cx, argc, argv, "js", &n, &line))
	return JS_FALSE;

    if (n <= 0 || n > buf->b_ml.ml_line_count)
    {
	JS_ReportError(cx, "index %d out of buffer", n);
	return JS_FALSE;
    }

    savebuf = curbuf;
    curbuf = buf;
    if (u_savesub(n) == FAIL)
	JS_ReportError(cx, "cannot save undo information");
    else if (ml_replace((linenr_T)n, (char_u *)line, TRUE) == FAIL)
	JS_ReportError(cx, "cannot replace line");
    else
	changed_bytes((linenr_T)n, 0);
    curbuf = savebuf;

    return JS_TRUE;
}

    static JSObject *
sm_buf_new(JSContext *cx, buf_T *buf)
{
    JSObject *bufobj;

    if (buf->b_spidermonkey_ref)
	 return (JSObject *)buf->b_spidermonkey_ref;

    bufobj = JS_NewObject(cx, &sm_buf_class, NULL, NULL);
    if (!bufobj)
	goto err;
    if (!JS_SetPrivate(cx, bufobj, buf))
	goto err;
    if (!JS_DefineProperties(cx, bufobj, sm_buf_props))
	goto err;
    if (!JS_DefineFunctions(cx, bufobj, sm_buf_methods))
	goto err;
    buf->b_spidermonkey_ref = bufobj;

    return bufobj;

err:
    JS_ReportError(cx, "cannot create buffer object");
    return NULL;
}

    static buf_T *
sm_buf_getptr(JSContext *cx, JSObject *obj)
{
    buf_T *buf = (buf_T *)JS_GetPrivate(cx, obj);
    if (buf == NULL)
	JS_ReportError(cx, "attempt to refer to deleted buffer");
    return buf;
}

/*
 * Check if deleting lines made the cursor position invalid.
 * Changed the lines from "lo" to "hi" and added "extra" lines (negative if
 * deleted).
 */
    static void
sm_fix_cursor(int lo, int hi, int extra)
{
    if (curwin->w_cursor.lnum >= lo)
    {
	/* Adjust the cursor position if it's in/after the changed
	 * lines. */
	if (curwin->w_cursor.lnum >= hi)
	{
	    curwin->w_cursor.lnum += extra;
	    check_cursor_col();
	}
	else if (extra < 0)
	{
	    curwin->w_cursor.lnum = lo;
	    check_cursor();
	}
	changed_cline_bef_curs();
    }
    invalidate_botline();
}

    static JSObject
*sm_windows_init(JSContext *cx, JSObject *obj)
{
    JSObject *windows = JS_DefineObject(cx, obj, "windows", &sm_windows_class, NULL, 0);
    if (!windows)
	return NULL;
    if (!JS_DefineProperties(cx, windows, sm_windows_props))
	return NULL;
    if (!JS_DefineFunctions(cx, windows, sm_windows_methods))
	return NULL;
    return windows;
}

    static JSBool
sm_windows_get(JSContext *cx, JSObject *obj, jsval id, jsval *vp)
{
    win_T *win;
    JSObject *winobj;

    win = sm_find_vimwin(id);
    if (!win)
	return JS_TRUE; /* return undefined */

    winobj = sm_win_new(cx, win);
    if (!winobj)
	return JS_FALSE;
    *vp = OBJECT_TO_JSVAL(winobj);
    return JS_TRUE;
}

    static JSBool
sm_windows_enum(JSContext *cx, JSObject *obj, JSIterateOp enum_op, jsval *statep, jsid *idp)
{
    switch (enum_op)
    {
	case JSENUMERATE_INIT:
	    *statep = JSVAL_ONE;
	    if (idp)
	    {
#ifdef FEAT_WINDOWS
		win_T *win;
		int n = 0;
		for (win = firstwin; win; win = win->w_next)
		    ++n;
		*idp = INT_TO_JSVAL(n);
#else
		*idp = JSVAL_ONE;
#endif
	    }
	    return JS_TRUE;
	case JSENUMERATE_NEXT:
	    if (sm_find_vimwin(*statep))
	    {
		if (!JS_DefineProperty(cx, obj,
			    (char *)JSVAL_TO_INT(*statep), JSVAL_VOID,
			    NULL, NULL, JSPROP_ENUMERATE | JSPROP_INDEX))
		    return JS_FALSE;
		*idp = *statep;
		*statep = INT_TO_JSVAL(JSVAL_TO_INT(*statep) + 1);
		return JS_TRUE;
	    }
	    /* fall through */
	case JSENUMERATE_DESTROY:
	    *statep = JSVAL_NULL;
	    return JS_TRUE;
	default:
	    return JS_FALSE;
    }
}


    static JSBool
sm_windows_prop_get(JSContext *cx, JSObject *obj, jsval id, jsval *vp)
{
    switch (JSVAL_TO_INT(id))
    {
	case SM_WINDOWS_COUNT:
#ifdef FEAT_WINDOWS
	    {
		win_T *win;
		int n = 0;
		for (win = firstwin; win; win = win->w_next)
		    ++n;
		*vp = INT_TO_JSVAL(n);
	    }
#else
	    *vp = JSVAL_ONE;
#endif
	    return JS_TRUE;
	case SM_WINDOWS_FIRST:
	    {
		JSObject *winobj = sm_win_new(cx, firstwin);
		if (!winobj)
		    return JS_FALSE;
		*vp = OBJECT_TO_JSVAL(winobj);
	    }
	    return JS_TRUE;
	case SM_WINDOWS_LAST:
	    {
		JSObject *winobj = sm_win_new(cx, lastwin);
		if (!winobj)
		    return JS_FALSE;
		*vp = OBJECT_TO_JSVAL(winobj);
	    }
	    return JS_TRUE;
	case SM_WINDOWS_CURRENT:
	    {
		JSObject *winobj = sm_win_new(cx, curwin);
		if (!winobj)
		    return JS_FALSE;
		*vp = OBJECT_TO_JSVAL(winobj);
	    }
	    return JS_TRUE;
	default:
	    return JS_FALSE;
    }
}

    static win_T *
sm_find_vimwin(jsval id)
{
    win_T *win = NULL;
    if (JSVAL_IS_INT(id))
    {
	int n = JSVAL_TO_INT(id);
#ifdef FEAT_WINDOWS
	if (n > 0)
	{
	    win = firstwin;
	    while (--n > 0 && win != NULL)
		win = win->w_next;
	}
#else
	if (n == 1)
	    win = curwin;
#endif
    }
    else if (JSVAL_IS_STRING(id))
    {
	char *str = JS_GetStringBytes(JSVAL_TO_STRING(id));
	if (strcmp(str, "%") == 0)
	    win = curwin;
	else if (strcmp(str, "$") == 0)
	    win = lastwin;
	else if (strcmp(str, "#") == 0)
	    win = prevwin;
    }
    return win;
}

    static void
sm_win_finalize(JSContext *cx, JSObject *obj)
{
    win_T *win = (win_T *)JS_GetPrivate(cx, obj);
    if (win != NULL)
	win->w_spidermonkey_ref = NULL;
}


    static JSBool
sm_win_prop_get(JSContext *cx, JSObject *obj, jsval id, jsval *vp)
{
    win_T *win = sm_win_getptr(cx, obj);
    if (win == NULL)
	return JS_FALSE;
    switch (JSVAL_TO_INT(id))
    {
	case SM_WIN_NUMBER:
#ifdef FEAT_WINDOWS
	    {
		int n = 0;
		for ( ; win; win = win->w_prev)
		    ++n;
		*vp = INT_TO_JSVAL(n);
	    }
#else
	    *vp = JSVAL_ONE;
#endif
	    return JS_TRUE;
	case SM_WIN_HEIGHT:
	    *vp = INT_TO_JSVAL(win->w_height);
	    return JS_TRUE;
	case SM_WIN_NEXT:
#ifdef FEAT_WINDOWS
	    if (win->w_next == NULL)
		*vp = JSVAL_VOID;
	    else
	    {
		JSObject *winobj = sm_win_new(cx, win->w_next);
		if (!winobj)
		    return JS_FALSE;
		*vp = OBJECT_TO_JSVAL(winobj);
	    }
#else
	    *vp = JSVAL_VOID;
#endif
	    return JS_TRUE;
	case SM_WIN_PREV:
#ifdef FEAT_WINDOWS
	    if (win->w_prev == NULL)
		*vp = JSVAL_VOID;
	    else
	    {
		JSObject *winobj = sm_win_new(cx, win->w_prev);
		if (!winobj)
		    return JS_FALSE;
		*vp = OBJECT_TO_JSVAL(winobj);
	    }
#else
	    *vp = JSVAL_VOID;
#endif
	    return JS_TRUE;
	case SM_WIN_BUFFER:
	    {
		JSObject *bufobj = sm_buf_new(cx, win->w_buffer);
		if (!bufobj)
		    return JS_FALSE;
		*vp = OBJECT_TO_JSVAL(bufobj);
	    }
	    return JS_TRUE;
	default:
	    return JS_FALSE;
    }
}

    static JSBool
sm_win_prop_set(JSContext *cx, JSObject *obj, jsval id, jsval *vp)
{
    win_T *win = sm_win_getptr(cx, obj);
    if (win == NULL)
	return JS_FALSE;
    switch (JSVAL_TO_INT(id))
    {
	case SM_WIN_HEIGHT:
	    {
		win_T *savewin = curwin;
		int32 n;
		if (!JS_ValueToInt32(cx, *vp, &n))
		    return JS_FALSE;
		curwin = win;
		win_setheight(n);
		curwin = savewin;
	    }
	    return JS_TRUE;
	default:
	    return JS_FALSE;
    }
}

    static JSObject *
sm_win_new(JSContext *cx, win_T *win)
{
    JSObject *winobj;

    if (win->w_spidermonkey_ref)
	 return (JSObject *)win->w_spidermonkey_ref;

    winobj = JS_NewObject(cx, &sm_win_class, NULL, NULL);
    if (!winobj)
	goto err;
    if (!JS_SetPrivate(cx, winobj, win))
	goto err;
    if (!JS_DefineProperties(cx, winobj, sm_win_props))
	goto err;
    if (!JS_DefineFunctions(cx, winobj, sm_win_methods))
	goto err;
    win->w_spidermonkey_ref = winobj;

    return winobj;

err:
    JS_ReportError(cx, "cannot create window object");
    return NULL;
}

    static win_T *
sm_win_getptr(JSContext *cx, JSObject *obj)
{
    win_T *win = (win_T *)JS_GetPrivate(cx, obj);
    if (win == NULL)
	JS_ReportError(cx, "attempt to refer to deleted window");
    return win;
}

