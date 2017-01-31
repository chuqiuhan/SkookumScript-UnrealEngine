//=======================================================================================
// SkookumScript Plugin for Unreal Engine 4
// Copyright (c) 2015 Agog Labs Inc. All rights reserved.
//
// Class for managing functions exposed to Blueprint graphs 
//
// Author: Markus Breyer
//=======================================================================================

#pragma once

//=======================================================================================
// Includes
//=======================================================================================

#include "UObject/WeakObjectPtr.h"
#include "UObject/Class.h"

#include <AgogCore/AFunctionArg.hpp>
#include <AgogCore/APArray.hpp>
#include <AgogCore/ASymbol.hpp>
#include <SkookumScript/SkClass.hpp>
#include <SkookumScript/SkInvokableBase.hpp>
#include <SkookumScript/SkMethod.hpp>
#include <SkookumScript/SkParameters.hpp>

class SkClass;
class SkClassDescBase;
class SkInstance;
class SkInvokedMethod;
struct FFrame;

typedef AFunctionArgBase<UClass *> tSkUEOnClassUpdatedFunc;

//---------------------------------------------------------------------------------------
// Class for managing functions exposed to Blueprint graphs
class SkUEBlueprintInterface
  {
  public:
              SkUEBlueprintInterface();
              ~SkUEBlueprintInterface();

    static SkUEBlueprintInterface * get() { return ms_singleton_p; }

    void      clear();
    void      clear_all_sk_invokables();
    UClass *  reexpose_class(SkClass * sk_class_p, tSkUEOnClassUpdatedFunc * on_class_updated_f, bool is_final);
    void      reexpose_class_recursively(SkClass * sk_class_p, tSkUEOnClassUpdatedFunc * on_class_updated_f, bool is_final);
    void      reexpose_all(tSkUEOnClassUpdatedFunc * on_class_updated_f, bool is_final); // Gather methods from SkookumScript
    void      rebind_all_sk_invokables();

    bool      is_skookum_blueprint_function(UFunction * function_p) const;
    bool      is_skookum_blueprint_event(UFunction * function_p) const;

  protected:

    // We place this magic number in the rep offset to be able to tell if a UFunction is an Sk event
    // Potential false positive is ok since we are using it only to select which graph nodes to update
    enum { EventMagicRepOffset = 0xBEEF };

    // To keep track of a parameter's name, size and type
    struct TypedName
      {
      ASymbol     m_name;
      uint32_t    m_byte_size;
      SkClass *   m_sk_class_p;

      TypedName(const ASymbol & name, uint32_t byte_size, SkClass * sk_class_p) : m_name(name), m_byte_size(byte_size), m_sk_class_p(sk_class_p) {}
      };

    typedef SkInstance *  (*tK2ParamFetcher)(FFrame & stack, const TypedName & typed_name);
    typedef uint32_t      (*tSkValueGetter)(void * const result_p, SkInstance * value_p, const TypedName & typed_name);

    enum eBindingType
      {
      BindingType_Function,  // Call from Blueprints into Sk
      BindingType_Event,     // Call from Sk into Blueprints
      };

    // Keep track of a binding between Blueprints and Sk
    struct BindingEntry
      {
      ASymbol                   m_sk_invokable_name;  // Copy of m_sk_invokable_p->get_name() in case m_sk_invokable_p goes bad
      ASymbol                   m_sk_class_name;      // Copy of m_sk_invokable_p->get_scope()->get_name() in case m_sk_invokable_p goes bad
      SkInvokableBase *         m_sk_invokable_p;
      TWeakObjectPtr<UClass>    m_ue_class_p;         // Copy of m_sk_invokable_p->GetOwnerClass() to detect if a deleted UFunction leaves dangling pointers
      TWeakObjectPtr<UFunction> m_ue_function_p;
      uint16_t                  m_num_params;
      bool                      m_is_class_member;    // Copy of m_sk_invokable_p->is_class_member() in case m_sk_invokable_p goes bad
      bool                      m_marked_for_delete;
      eBindingType              m_type;

      BindingEntry(SkInvokableBase * sk_invokable_p, UFunction * ue_method_p, uint32_t num_params, eBindingType type)
        : m_sk_invokable_name(sk_invokable_p->get_name())
        , m_sk_class_name(sk_invokable_p->get_scope()->get_name())
        , m_sk_invokable_p(sk_invokable_p)
        , m_ue_class_p(ue_method_p->GetOwnerClass())
        , m_ue_function_p(ue_method_p)
        , m_num_params(num_params)
        , m_is_class_member(sk_invokable_p->is_class_member())
        , m_marked_for_delete(false)
        , m_type(type)
        {}
      };

    // Parameter being passed into Sk from Blueprints
    struct SkParamEntry : TypedName
      {
      tK2ParamFetcher m_fetcher_p;

      SkParamEntry(const ASymbol & name, uint32_t byte_size, SkClass * sk_class_p, tK2ParamFetcher fetcher_p) : TypedName(name, byte_size, sk_class_p), m_fetcher_p(fetcher_p) {}
      };

    // Function binding (call from Blueprints into Sk)
    struct FunctionEntry : public BindingEntry
      {
      TypedName       m_result_type;
      tSkValueGetter  m_result_getter;

      FunctionEntry(SkInvokableBase * sk_invokable_p, UFunction * ue_function_p, uint32_t num_params, SkClass * result_sk_class_p, uint32_t result_byte_size, tSkValueGetter result_getter)
        : BindingEntry(sk_invokable_p, ue_function_p, num_params, BindingType_Function)
        , m_result_type(ASymbol::ms_null, result_byte_size, result_sk_class_p)
        , m_result_getter(result_getter)
        {}

      // The parameter entries are stored behind this structure in memory
      SkParamEntry *       get_param_entry_array()       { return (SkParamEntry *)(this + 1); }
      const SkParamEntry * get_param_entry_array() const { return (const SkParamEntry *)(this + 1); }
      };

    // Parameter being passed into Blueprints into Sk
    struct K2ParamEntry : TypedName
      {
      tSkValueGetter  m_getter_p;
      uint32_t        m_offset;

      K2ParamEntry(const ASymbol & name, uint32_t byte_size, SkClass * sk_class_p, tSkValueGetter getter_p, uint32_t offset) : TypedName(name, byte_size, sk_class_p), m_getter_p(getter_p), m_offset(offset) {}
      };

    // Event binding (call from Sk into Blueprints)
    struct EventEntry : public BindingEntry
      {
      mutable TWeakObjectPtr<UFunction> m_ue_function_to_invoke_p; // The copy of our method we actually can invoke

      EventEntry(SkMethodBase * sk_method_p, UFunction * ue_function_p, uint32_t num_params)
        : BindingEntry(sk_method_p, ue_function_p, num_params, BindingType_Event)
        {}

      // The parameter entries are stored behind this structure in memory
      K2ParamEntry *       get_param_entry_array()       { return (K2ParamEntry *)(this + 1); }
      const K2ParamEntry * get_param_entry_array() const { return (const K2ParamEntry *)(this + 1); }
      };

    struct ParamInfo
      {
      UProperty *       m_ue_param_p;
      tK2ParamFetcher   m_k2_param_fetcher_p;
      tSkValueGetter    m_sk_value_getter_p;
      };

    void                exec_method(FFrame & stack, void * const result_p, SkClass * class_scope_p, SkInstance * this_p);
    void                exec_class_method(FFrame & stack, void * const result_p);
    void                exec_instance_method(FFrame & stack, void * const result_p);
    void                exec_coroutine(FFrame & stack, void * const result_p);

    static void         mthd_trigger_event(SkInvokedMethod * scope_p, SkInstance ** result_pp);

    bool                reexpose_class(SkClass * sk_class_p, UClass * ue_class_p, tSkUEOnClassUpdatedFunc * on_class_updated_f, bool is_final);
    bool                try_update_binding_entry(UClass * ue_class_p, SkInvokableBase * sk_invokable_p, int32_t * out_binding_index_p);
    int32_t             try_add_binding_entry(UClass * ue_class_p, SkInvokableBase * sk_invokable_p, bool is_final);
    int32_t             add_function_entry(UClass * ue_class_p, SkInvokableBase * sk_invokable_p, bool is_final);
    int32_t             add_event_entry(UClass * ue_class_p, SkMethodBase * sk_method_p, bool is_final);
    int32_t             store_binding_entry(BindingEntry * binding_entry_p, int32_t binding_index_to_use);
    void                delete_binding_entry(uint32_t binding_index);
    UFunction *         build_ue_function(UClass * ue_class_p, SkInvokableBase * sk_invokable_p, eBindingType binding_type, ParamInfo * out_param_info_array_p, bool is_final);
    UProperty *         build_ue_param(UFunction * ue_function_p, SkClassDescBase * sk_parameter_class_p, const FName & param_name, ParamInfo * out_param_info_p, bool is_final);
    void                bind_event_method(SkMethodBase * sk_method_p);
    
    template<class _TypedName>
    static bool         have_identical_signatures(const tSkParamList & param_list, const _TypedName * param_array_p);

    static SkInstance * fetch_k2_param_boolean(FFrame & stack, const TypedName & typed_name);
    static SkInstance * fetch_k2_param_integer(FFrame & stack, const TypedName & typed_name);
    static SkInstance * fetch_k2_param_real(FFrame & stack, const TypedName & typed_name);
    static SkInstance * fetch_k2_param_string(FFrame & stack, const TypedName & typed_name);
    static SkInstance * fetch_k2_param_vector3(FFrame & stack, const TypedName & typed_name);
    static SkInstance * fetch_k2_param_rotation_angles(FFrame & stack, const TypedName & typed_name);
    static SkInstance * fetch_k2_param_transform(FFrame & stack, const TypedName & typed_name);
    static SkInstance * fetch_k2_param_struct_val(FFrame & stack, const TypedName & typed_name);
    static SkInstance * fetch_k2_param_struct_ref(FFrame & stack, const TypedName & typed_name);
    static SkInstance * fetch_k2_param_entity(FFrame & stack, const TypedName & typed_name);

    static uint32_t     get_sk_value_boolean(void * const result_p, SkInstance * value_p, const TypedName & typed_name);
    static uint32_t     get_sk_value_integer(void * const result_p, SkInstance * value_p, const TypedName & typed_name);
    static uint32_t     get_sk_value_real(void * const result_p, SkInstance * value_p, const TypedName & typed_name);
    static uint32_t     get_sk_value_string(void * const result_p, SkInstance * value_p, const TypedName & typed_name);
    static uint32_t     get_sk_value_vector3(void * const result_p, SkInstance * value_p, const TypedName & typed_name);
    static uint32_t     get_sk_value_rotation_angles(void * const result_p, SkInstance * value_p, const TypedName & typed_name);
    static uint32_t     get_sk_value_transform(void * const result_p, SkInstance * value_p, const TypedName & typed_name);
    static uint32_t     get_sk_value_struct_val(void * const result_p, SkInstance * value_p, const TypedName & typed_name);
    static uint32_t     get_sk_value_struct_ref(void * const result_p, SkInstance * value_p, const TypedName & typed_name);
    static uint32_t     get_sk_value_entity(void * const result_p, SkInstance * value_p, const TypedName & typed_name);

    APArray<BindingEntry> m_binding_entry_array;

    UScriptStruct *       m_struct_vector3_p;
    UScriptStruct *       m_struct_rotation_angles_p;
    UScriptStruct *       m_struct_transform_p;

    static SkUEBlueprintInterface * ms_singleton_p; // Hack, make it easy to access for callbacks
        
  };