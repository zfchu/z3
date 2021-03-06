/*++
Copyright (c) 2012 Microsoft Corporation

Module Name:

    pb2bv_model_converter.h

Abstract:

    Model converter for the pb2bv tactic.

Author:

    Christoph (cwinter) 2012-02-15

Notes:

--*/
#ifndef PB2BV_MODEL_CONVERTER_H_
#define PB2BV_MODEL_CONVERTER_H_

#include "tactic/model_converter.h"
#include "tactic/arith/bound_manager.h"

class pb2bv_model_converter : public model_converter {
    typedef std::pair<func_decl *, func_decl *> func_decl_pair;
    
    ast_manager &             m;    
    svector<func_decl_pair>   m_c2bit;
public:
    pb2bv_model_converter(ast_manager & _m);
    pb2bv_model_converter(ast_manager & _m, obj_map<func_decl, expr*> const & c2bit, bound_manager const & bm);
    ~pb2bv_model_converter() override;
    void operator()(model_ref & md) override;
    void operator()(model_ref & md, unsigned goal_idx) override;
    void display(std::ostream & out) override;
    model_converter * translate(ast_translation & translator) override;
};

#endif
