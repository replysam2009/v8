// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/runtime/runtime-utils.h"

#include "src/arguments.h"
#include "src/debug/debug.h"
#include "src/debug/debug-frames.h"
#include "src/debug/liveedit.h"
#include "src/frames-inl.h"
#include "src/isolate-inl.h"
#include "src/runtime/runtime.h"

namespace v8 {
namespace internal {

// For a script finds all SharedFunctionInfo's in the heap that points
// to this script. Returns JSArray of SharedFunctionInfo wrapped
// in OpaqueReferences.
RUNTIME_FUNCTION(Runtime_LiveEditFindSharedFunctionInfosForScript) {
  HandleScope scope(isolate);
  CHECK(isolate->debug()->live_edit_enabled());
  DCHECK_EQ(1, args.length());
  CONVERT_ARG_CHECKED(JSValue, script_value, 0);

  CHECK(script_value->value()->IsScript());
  Handle<Script> script(Script::cast(script_value->value()), isolate);

  std::vector<Handle<SharedFunctionInfo>> found;
  Heap* heap = isolate->heap();
  {
    HeapIterator iterator(heap);
    HeapObject* heap_obj;
    while ((heap_obj = iterator.next()) != nullptr) {
      if (!heap_obj->IsSharedFunctionInfo()) continue;
      SharedFunctionInfo* shared = SharedFunctionInfo::cast(heap_obj);
      if (shared->script() != *script) continue;
      found.push_back(Handle<SharedFunctionInfo>(shared, isolate));
    }
  }

  int found_size = static_cast<int>(found.size());
  Handle<FixedArray> result = isolate->factory()->NewFixedArray(found_size);
  for (int i = 0; i < found_size; ++i) {
    Handle<SharedFunctionInfo> shared = found[i];
    SharedInfoWrapper info_wrapper = SharedInfoWrapper::Create(isolate);
    Handle<String> name(shared->Name(), isolate);
    info_wrapper.SetProperties(name, shared->StartPosition(),
                               shared->EndPosition(), shared);
    result->set(i, *info_wrapper.GetJSArray());
  }
  return *isolate->factory()->NewJSArrayWithElements(result);
}


// For a script calculates compilation information about all its functions.
// The script source is explicitly specified by the second argument.
// The source of the actual script is not used, however it is important that
// all generated code keeps references to this particular instance of script.
// Returns a JSArray of compilation infos. The array is ordered so that
// each function with all its descendant is always stored in a continues range
// with the function itself going first. The root function is a script function.
RUNTIME_FUNCTION(Runtime_LiveEditGatherCompileInfo) {
  HandleScope scope(isolate);
  CHECK(isolate->debug()->live_edit_enabled());
  DCHECK_EQ(2, args.length());
  CONVERT_ARG_CHECKED(JSValue, script, 0);
  CONVERT_ARG_HANDLE_CHECKED(String, source, 1);

  CHECK(script->value()->IsScript());
  Handle<Script> script_handle(Script::cast(script->value()), isolate);

  RETURN_RESULT_OR_FAILURE(isolate,
                           LiveEdit::GatherCompileInfo(script_handle, source));
}


// Changes the source of the script to a new_source.
// If old_script_name is provided (i.e. is a String), also creates a copy of
// the script with its original source and sends notification to debugger.
RUNTIME_FUNCTION(Runtime_LiveEditReplaceScript) {
  HandleScope scope(isolate);
  CHECK(isolate->debug()->live_edit_enabled());
  DCHECK_EQ(3, args.length());
  CONVERT_ARG_CHECKED(JSValue, original_script_value, 0);
  CONVERT_ARG_HANDLE_CHECKED(String, new_source, 1);
  CONVERT_ARG_HANDLE_CHECKED(Object, old_script_name, 2);

  CHECK(original_script_value->value()->IsScript());
  Handle<Script> original_script(Script::cast(original_script_value->value()));

  Handle<Object> old_script = LiveEdit::ChangeScriptSource(
      original_script, new_source, old_script_name);

  if (old_script->IsScript()) {
    Handle<Script> script_handle = Handle<Script>::cast(old_script);
    return *Script::GetWrapper(script_handle);
  } else {
    return isolate->heap()->null_value();
  }
}

// Recreate the shared function infos array after changing the IDs of all
// SharedFunctionInfos.
RUNTIME_FUNCTION(Runtime_LiveEditFixupScript) {
  HandleScope scope(isolate);
  CHECK(isolate->debug()->live_edit_enabled());
  DCHECK_EQ(args.length(), 2);
  CONVERT_ARG_CHECKED(JSValue, script_value, 0);
  CONVERT_INT32_ARG_CHECKED(max_function_literal_id, 1);

  CHECK(script_value->value()->IsScript());
  Handle<Script> script(Script::cast(script_value->value()));

  LiveEdit::FixupScript(script, max_function_literal_id);
  return isolate->heap()->undefined_value();
}

RUNTIME_FUNCTION(Runtime_LiveEditFunctionSourceUpdated) {
  HandleScope scope(isolate);
  CHECK(isolate->debug()->live_edit_enabled());
  DCHECK_EQ(args.length(), 2);
  CONVERT_ARG_HANDLE_CHECKED(JSArray, shared_info, 0);
  CONVERT_INT32_ARG_CHECKED(new_function_literal_id, 1);
  CHECK(SharedInfoWrapper::IsInstance(shared_info));

  LiveEdit::FunctionSourceUpdated(shared_info, new_function_literal_id);
  return isolate->heap()->undefined_value();
}


// Replaces code of SharedFunctionInfo with a new one.
RUNTIME_FUNCTION(Runtime_LiveEditReplaceFunctionCode) {
  HandleScope scope(isolate);
  CHECK(isolate->debug()->live_edit_enabled());
  DCHECK_EQ(2, args.length());
  CONVERT_ARG_HANDLE_CHECKED(JSArray, new_compile_info, 0);
  CONVERT_ARG_HANDLE_CHECKED(JSArray, shared_info, 1);
  CHECK(SharedInfoWrapper::IsInstance(shared_info));

  LiveEdit::ReplaceFunctionCode(new_compile_info, shared_info);
  return isolate->heap()->undefined_value();
}


// Connects SharedFunctionInfo to another script.
RUNTIME_FUNCTION(Runtime_LiveEditFunctionSetScript) {
  HandleScope scope(isolate);
  CHECK(isolate->debug()->live_edit_enabled());
  DCHECK_EQ(2, args.length());
  CONVERT_ARG_HANDLE_CHECKED(Object, function_object, 0);
  CONVERT_ARG_HANDLE_CHECKED(Object, script_object, 1);

  if (function_object->IsJSValue()) {
    Handle<JSValue> function_wrapper = Handle<JSValue>::cast(function_object);
    if (script_object->IsJSValue()) {
      CHECK(JSValue::cast(*script_object)->value()->IsScript());
      Script* script = Script::cast(JSValue::cast(*script_object)->value());
      script_object = Handle<Object>(script, isolate);
    }
    CHECK(function_wrapper->value()->IsSharedFunctionInfo());
    LiveEdit::SetFunctionScript(function_wrapper, script_object);
  } else {
    // Just ignore this. We may not have a SharedFunctionInfo for some functions
    // and we check it in this function.
  }

  return isolate->heap()->undefined_value();
}


// In a code of a parent function replaces original function as embedded object
// with a substitution one.
RUNTIME_FUNCTION(Runtime_LiveEditReplaceRefToNestedFunction) {
  HandleScope scope(isolate);
  CHECK(isolate->debug()->live_edit_enabled());
  DCHECK_EQ(3, args.length());

  CONVERT_ARG_HANDLE_CHECKED(JSValue, parent_wrapper, 0);
  CONVERT_ARG_HANDLE_CHECKED(JSValue, orig_wrapper, 1);
  CONVERT_ARG_HANDLE_CHECKED(JSValue, subst_wrapper, 2);
  CHECK(parent_wrapper->value()->IsSharedFunctionInfo());
  CHECK(orig_wrapper->value()->IsSharedFunctionInfo());
  CHECK(subst_wrapper->value()->IsSharedFunctionInfo());

  LiveEdit::ReplaceRefToNestedFunction(isolate->heap(), parent_wrapper,
                                       orig_wrapper, subst_wrapper);
  return isolate->heap()->undefined_value();
}


// Updates positions of a shared function info (first parameter) according
// to script source change. Text change is described in second parameter as
// array of groups of 3 numbers:
// (change_begin, change_end, change_end_new_position).
// Each group describes a change in text; groups are sorted by change_begin.
RUNTIME_FUNCTION(Runtime_LiveEditPatchFunctionPositions) {
  HandleScope scope(isolate);
  CHECK(isolate->debug()->live_edit_enabled());
  DCHECK_EQ(2, args.length());
  CONVERT_ARG_HANDLE_CHECKED(JSArray, shared_array, 0);
  CONVERT_ARG_HANDLE_CHECKED(JSArray, position_change_array, 1);
  CHECK(SharedInfoWrapper::IsInstance(shared_array));

  LiveEdit::PatchFunctionPositions(shared_array, position_change_array);
  return isolate->heap()->undefined_value();
}


// For array of SharedFunctionInfo's (each wrapped in JSValue)
// checks that none of them have activations on stacks (of any thread).
// Returns array of the same length with corresponding results of
// LiveEdit::FunctionPatchabilityStatus type.
RUNTIME_FUNCTION(Runtime_LiveEditCheckAndDropActivations) {
  HandleScope scope(isolate);
  CHECK(isolate->debug()->live_edit_enabled());
  DCHECK_EQ(3, args.length());
  CONVERT_ARG_HANDLE_CHECKED(JSArray, old_shared_array, 0);
  CONVERT_ARG_HANDLE_CHECKED(JSArray, new_shared_array, 1);
  CONVERT_BOOLEAN_ARG_CHECKED(do_drop, 2);
  USE(new_shared_array);
  CHECK(old_shared_array->length()->IsSmi());
  CHECK(new_shared_array->length() == old_shared_array->length());
  CHECK(old_shared_array->HasFastElements());
  CHECK(new_shared_array->HasFastElements());
  int array_length = Smi::ToInt(old_shared_array->length());
  for (int i = 0; i < array_length; i++) {
    Handle<Object> old_element;
    Handle<Object> new_element;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, old_element,
        JSReceiver::GetElement(isolate, old_shared_array, i));
    CHECK(old_element->IsJSValue() &&
          Handle<JSValue>::cast(old_element)->value()->IsSharedFunctionInfo());
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, new_element,
        JSReceiver::GetElement(isolate, new_shared_array, i));
    CHECK(
        new_element->IsUndefined(isolate) ||
        (new_element->IsJSValue() &&
         Handle<JSValue>::cast(new_element)->value()->IsSharedFunctionInfo()));
  }

  return *LiveEdit::CheckAndDropActivations(old_shared_array, new_shared_array,
                                            do_drop);
}


// Compares 2 strings line-by-line, then token-wise and returns diff in form
// of JSArray of triplets (pos1, pos1_end, pos2_end) describing list
// of diff chunks.
RUNTIME_FUNCTION(Runtime_LiveEditCompareStrings) {
  HandleScope scope(isolate);
  CHECK(isolate->debug()->live_edit_enabled());
  DCHECK_EQ(2, args.length());
  CONVERT_ARG_HANDLE_CHECKED(String, s1, 0);
  CONVERT_ARG_HANDLE_CHECKED(String, s2, 1);

  Handle<JSArray> result = LiveEdit::CompareStrings(s1, s2);
  uint32_t array_length = 0;
  CHECK(result->length()->ToArrayLength(&array_length));
  if (array_length > 0) {
    isolate->debug()->feature_tracker()->Track(DebugFeatureTracker::kLiveEdit);
  }

  return *result;
}
}  // namespace internal
}  // namespace v8
