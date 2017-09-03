/*++
Copyright (c) 2006 Microsoft Corporation

Module Name:

    datatype_decl_plugin.cpp

Abstract:

    <abstract>

Author:

    Leonardo de Moura (leonardo) 2008-01-10.

Revision History:

--*/
#include "util/warning.h"
#include "ast/datatype_decl_plugin2.h"
#include "ast/array_decl_plugin.h"
#include "ast/ast_smt2_pp.h"


namespace datatype {

    func_decl_ref accessor::instantiate(sort_ref_vector const& ps) const {
        unsigned n = ps.size();
        SASSERT(n == get_def().params().size());
        sort_ref range(m.substitute(m_range, n, get_def().params().c_ptr(), ps.c_ptr()), m);
        sort_ref src(get_def().instantiate(ps));
        sort* srcs[1] = { src.get() };
        parameter pas[2] = { parameter(name()), parameter(get_constructor().name()) };
        return func_decl_ref(m.mk_func_decl(u().get_family_id(), OP_DT_ACCESSOR, 2, pas, 1, srcs, range), m);
    }

    func_decl_ref accessor::instantiate(sort* dt) const {
        sort_ref_vector sorts = get_def().u().datatype_params(dt);
        return instantiate(sorts);
    }

    def const& accessor::get_def() const { return m_constructor->get_def(); }
    util& accessor::u() const { return m_constructor->u(); }

    util& constructor::u() const { return m_def->u(); }

    func_decl_ref constructor::instantiate(sort_ref_vector const& ps) const {
        sort_ref_vector domain(m);
        for (accessor const& a : accessors()) {
            domain.push_back(a.instantiate(ps)->get_range());
        }
        sort_ref range = get_def().instantiate(ps);
        parameter pas[1] = { parameter(name()) };
        return func_decl_ref(m.mk_func_decl(u().get_family_id(), OP_DT_CONSTRUCTOR, 1, pas, domain.size(), domain.c_ptr(), range), m);        
    }

    func_decl_ref constructor::instantiate(sort* dt) const {
        sort_ref_vector sorts = get_def().u().datatype_params(dt);
        return instantiate(sorts);
    }

    sort_ref def::instantiate(sort_ref_vector const& sorts) const {
        sort_ref s(m);
        if (!m_sort) {
            vector<parameter> ps;
            for (sort * s : m_params) ps.push_back(parameter(s));
            m_sort = m.mk_sort(u().get_family_id(), DATATYPE_SORT, ps.size(), ps.c_ptr());
        }
        if (sorts.empty()) {
            return m_sort;
        }
        return sort_ref(m.substitute(m_sort, sorts.size(), sorts.c_ptr(), m_params.c_ptr()), m);
    }

    enum status {
        GRAY,
        BLACK
    };

    namespace param_size {
        size* size::mk_offset(sort_size const& s) { return alloc(offset, s); }
        size* size::mk_param(sort_ref& p) { return alloc(sparam, p); }
        size* size::mk_plus(size* a1, size* a2) { return alloc(plus, a1, a2); }
        size* size::mk_times(size* a1, size* a2) { return alloc(times, a1, a2); }
        size* size::mk_times(ptr_vector<size>& szs) {
            if (szs.empty()) return mk_offset(sort_size(1));
            if (szs.size() == 1) return szs[0];
            size* r = szs[0];
            for (unsigned i = 1; i < szs.size(); ++i) {
                r = mk_times(r, szs[i]);
            }
            return r;
        }
        size* size::mk_plus(ptr_vector<size>& szs) {
            if (szs.empty()) return mk_offset(sort_size(0));
            if (szs.size() == 1) return szs[0];
            size* r = szs[0];
            for (unsigned i = 1; i < szs.size(); ++i) {
                r = mk_plus(r, szs[i]);
            }
            return r;
        }
        size* size::mk_power(size* a1, size* a2) { return alloc(power, a1, a2); }
    }

    namespace decl {

        plugin::~plugin() {
            finalize();
        }

        void plugin::finalize() {
            for (auto& kv : m_defs) {
                dealloc(kv.m_value);
            }
            m_defs.reset();
            m_util = 0; // force deletion
        }

        util & plugin::u() const {
            SASSERT(m_manager);
            if (m_util.get() == 0) {
                m_util = alloc(util, *m_manager);
            }
            return *(m_util.get());
        }

        struct invalid_datatype {};

        sort * plugin::mk_sort(decl_kind k, unsigned num_parameters, parameter const * parameters) {
            try {
                if (k != DATATYPE_SORT) {
                    throw invalid_datatype();
                }
                if (num_parameters < 1) {
                    throw invalid_datatype();                    
                }
                parameter const & name = parameters[0];
                if (!name.is_symbol()) {
                    TRACE("datatype", tout << "expected symol parameter at position " << 0 << " got: " << name << "\n";);
                    throw invalid_datatype();
                }
                for (unsigned i = 1; i < num_parameters; ++i) {
                    parameter const& s = parameters[i];
                    if (!s.is_ast() || !is_sort(s.get_ast())) {
                        TRACE("datatype", tout << "expected sort parameter at position " << i << " got: " << s << "\n";);
                        throw invalid_datatype();
                    }
                }
                                
                sort* s = m_manager->mk_sort(name.get_symbol(),
                                             sort_info(m_family_id, k, num_parameters, parameters, true));
                def* d = 0;
                if (m_defs.find(s->get_name(), d) && d->sort_size()) {
                    obj_map<sort, sort_size> S;
                    for (unsigned i = 1; i < num_parameters; ++i) {
                        sort* r = to_sort(parameters[i].get_ast());
                        S.insert(d->params()[i], r->get_num_elements()); 
                    }
                    sort_size ts = d->sort_size()->fold(S);
                    s->set_num_elements(ts);
                }
                return s;
            }
            catch (invalid_datatype) {
                m_manager->raise_exception("invalid datatype");
                return 0;
            }
        }

        func_decl * plugin::mk_update_field(
            unsigned num_parameters, parameter const * parameters, 
            unsigned arity, sort * const * domain, sort * range) {
            decl_kind k = OP_DT_UPDATE_FIELD;
            ast_manager& m = *m_manager;
            
            if (num_parameters != 1 || !parameters[0].is_ast()) {
                m.raise_exception("invalid parameters for datatype field update");
                return 0;
            }
            if (arity != 2) {
                m.raise_exception("invalid number of arguments for datatype field update");
                return 0;
            }
            func_decl* acc = 0;
            if (is_func_decl(parameters[0].get_ast())) {
                acc = to_func_decl(parameters[0].get_ast());
            }
            if (acc && !u().is_accessor(acc)) {
                acc = 0;
            }
            if (!acc) {
                m.raise_exception("datatype field update requires a datatype accessor as the second argument");
                return 0;
            }
            sort* dom = acc->get_domain(0);
            sort* rng = acc->get_range();
            if (dom != domain[0]) {
                m.raise_exception("first argument to field update should be a data-type");
                return 0;
            }
            if (rng != domain[1]) {
                std::ostringstream buffer;
                buffer << "second argument to field update should be " << mk_ismt2_pp(rng, m) 
                       << " instead of " << mk_ismt2_pp(domain[1], m);
                m.raise_exception(buffer.str().c_str());
                return 0;
            }
            range = domain[0];
            func_decl_info info(m_family_id, k, num_parameters, parameters);
            return m.mk_func_decl(symbol("update-field"), arity, domain, range, info);
        }

        
        func_decl * decl::plugin::mk_constructor(unsigned num_parameters, parameter const * parameters, 
                                                 unsigned arity, sort * const * domain, sort * range) {
            ast_manager& m = *m_manager;
            SASSERT(num_parameters == 1 && parameters[0].is_symbol() && range && u().is_datatype(range));
            if (num_parameters != 1 || !parameters[0].is_symbol() || !range || !u().is_datatype(range)) {
                m_manager->raise_exception("invalid parameters for datatype constructor");
            }
            // we blindly trust other conditions are met, including domain types.
            symbol name = parameters[0].get_symbol();
            func_decl_info info(m_family_id, OP_DT_CONSTRUCTOR, num_parameters, parameters);
            info.m_private_parameters = true;
            return m.mk_func_decl(name, arity, domain, range, info);
        }

        func_decl * decl::plugin::mk_recognizer(unsigned num_parameters, parameter const * parameters, 
                                                unsigned arity, sort * const * domain, sort *) {
            ast_manager& m = *m_manager;
            SASSERT(arity == 1 && num_parameters == 1 && parameters[0].is_ast() && is_func_decl(parameters[0].get_ast()));
            SASSERT(u().is_datatype(domain[0]));
            // blindly trust that parameter is a constructor
            sort* range = m_manager->mk_bool_sort();
            func_decl_info info(m_family_id, OP_DT_RECOGNISER, num_parameters, parameters);
            info.m_private_parameters = true;
            symbol name = to_func_decl(parameters[0].get_ast())->get_name();
            return m.mk_func_decl(name, arity, domain, range);
        }

        func_decl * decl::plugin::mk_accessor(unsigned num_parameters, parameter const * parameters, 
                                              unsigned arity, sort * const * domain, sort * range) 
        {            
            ast_manager& m = *m_manager;
            SASSERT(arity == 1 && num_parameters == 2 && parameters[0].is_symbol() && parameters[0].is_symbol());
            SASSERT(u().is_datatype(domain[0]));
            SASSERT(range);
            func_decl_info info(m_family_id, OP_DT_ACCESSOR, num_parameters, parameters);
            info.m_private_parameters = true;
            symbol name = parameters[0].get_symbol();
            return m.mk_func_decl(name, arity, domain, range);            
        }

        func_decl * decl::plugin::mk_func_decl(decl_kind k, unsigned num_parameters, parameter const * parameters, 
                                               unsigned arity, sort * const * domain, sort * range) {                        
            switch (k) {
            case OP_DT_CONSTRUCTOR:
                return mk_constructor(num_parameters, parameters, arity, domain, range);
            case OP_DT_RECOGNISER:
                return mk_recognizer(num_parameters, parameters, arity, domain, range);                
            case OP_DT_ACCESSOR:
                return mk_accessor(num_parameters, parameters, arity, domain, range);                
            case OP_DT_UPDATE_FIELD: 
                return mk_update_field(num_parameters, parameters, arity, domain, range);
            default:
                m_manager->raise_exception("invalid datatype operator kind");
                return 0;
            }
        }

        def& plugin::add(symbol const& name, unsigned n, sort * const * params) {
            ast_manager& m = *m_manager;
            def* d = alloc(def, m, u(), name, m_class_id, n, params);
            m_defs.insert(name, d);
            m_def_block.push_back(name);
            return *d;
        }

        void plugin::end_def_block() {
            ast_manager& m = *m_manager;
            sort_ref_vector sorts(m);
            for (symbol const& s : m_def_block) {
                def const& d = *m_defs[s];
                sort_ref_vector ps(m);
                sorts.push_back(d.instantiate(ps));
            }
            if (!u().is_well_founded(sorts.size(), sorts.c_ptr())) {
                m_manager->raise_exception("datatype is not well-founded");
            }
            u().compute_datatype_size_functions(m_def_block);
        }

        void plugin::del(symbol const& s) {
            def* d = 0;
            if (m_defs.find(s, d)) dealloc(d);
            m_defs.remove(s);
        }

        bool plugin::is_value_visit(expr * arg, ptr_buffer<app> & todo) const {
            if (!is_app(arg))
                return false;
            family_id fid = to_app(arg)->get_family_id();
            if (fid == m_family_id) {
                if (!u().is_constructor(to_app(arg)))
                    return false;
                if (to_app(arg)->get_num_args() == 0)
                    return true;
                todo.push_back(to_app(arg));
                return true;
            }
            else {
                return m_manager->is_value(arg);
            }
        }
        
        bool plugin::is_value(app * e) const {
            TRACE("dt_is_value", tout << "checking\n" << mk_ismt2_pp(e, *m_manager) << "\n";);
            if (!u().is_constructor(e))
                return false;
            if (e->get_num_args() == 0)
                return true;
            // REMARK: if the following check is too expensive, we should
            // cache the values in the decl::plugin.
            ptr_buffer<app> todo;
            // potentially expensive check for common sub-expressions.
            for (expr* arg : *e) {
                if (!is_value_visit(arg, todo)) {
                    TRACE("dt_is_value", tout << "not-value:\n" << mk_ismt2_pp(arg, *m_manager) << "\n";);
                    return false;
                }
            }
            while (!todo.empty()) {
                app * curr = todo.back();
                SASSERT(u().is_constructor(curr));
                todo.pop_back();
                for (expr* arg : *curr) {
                    if (!is_value_visit(arg, todo)) {
                        TRACE("dt_is_value", tout << "not-value:\n" << mk_ismt2_pp(arg, *m_manager) << "\n";);
                        return false;
                    }
                }
            }
            return true;
        }
        
        void plugin::get_op_names(svector<builtin_name> & op_names, symbol const & logic) {
            if (logic == symbol::null) {
                op_names.push_back(builtin_name("update-field", OP_DT_UPDATE_FIELD));
            }
        }

        expr * plugin::get_some_value(sort * s) {
            SASSERT(u().is_datatype(s));
            func_decl * c = u().get_non_rec_constructor(s);
            ptr_buffer<expr> args;
            for (unsigned i = 0; i < c->get_arity(); i++) {
                args.push_back(m_manager->get_some_value(c->get_domain(i)));
            }
            return m_manager->mk_app(c, args.size(), args.c_ptr());
        }

        bool plugin::is_fully_interp(sort * s) const {
            return u().is_fully_interp(s);
        }
    }

    sort_ref_vector util::datatype_params(sort * s) const {
        SASSERT(is_datatype(s));
        sort_ref_vector result(m);
        for (unsigned i = 1; i < s->get_num_parameters(); ++i) {
            result.push_back(to_sort(s->get_parameter(i).get_ast()));
        }
        return result;
    }


    bool util::is_fully_interp(sort * s) const {
        SASSERT(is_datatype(s));
        bool fi = true;
        return fi;
        if (m_is_fully_interp.find(s, fi)) {
            return fi;
        }
        unsigned sz = m_fully_interp_trail.size();
        m_is_fully_interp.insert(s, true);
        def const& d = get_def(s);
        bool is_interp = true;
        m_fully_interp_trail.push_back(s);
        for (constructor const& c : d) {
            for (accessor const& a : c) {
                func_decl_ref ac = a.instantiate(s);
                sort* r = ac->get_range();
                if (!m.is_fully_interp(r)) {
                    is_interp = false;
                    break;
                }
            }
            if (!is_interp) break;
        }
        for (unsigned i = sz; i < m_fully_interp_trail.size(); ++i) {
            m_is_fully_interp.remove(m_fully_interp_trail[i]);
        }
        m_fully_interp_trail.shrink(sz);
        m_is_fully_interp.insert(s, is_interp);
        m_asts.push_back(s);
        return true;
    }

    /**
       \brief Return true if the inductive datatype is recursive.
    */
    bool util::is_recursive_core(sort* s) const {
        obj_map<sort, status> already_found;
        ptr_vector<sort> todo, subsorts;
        todo.push_back(s);
        status st;
        while (!todo.empty()) {
            s = todo.back();
            if (already_found.find(s, st) && st == BLACK) {
                todo.pop_back();
                continue;
            }
            already_found.insert(s, GRAY);
            def const& d = get_def(s);
            bool can_process       = true;
            for (constructor const& c : d) {
                for (accessor const& a : c) {
                    sort* d = a.range();
                    // check if d is a datatype sort
                    subsorts.reset();
                    get_subsorts(d, subsorts);
                    for (sort * s2 : subsorts) {
                        if (is_datatype(s2)) {
                            if (already_found.find(s2, st)) {
                                // type is recursive
                                if (st == GRAY) return true;
                            }
                            else {
                                todo.push_back(s2);
                                can_process = false;
                            }
                        }
                    }
                }
            }
            if (can_process) {
                already_found.insert(s, BLACK);
                todo.pop_back();
            }
        }
        return false;
    }

    unsigned util::get_datatype_num_parameter_sorts(sort * ty) {
        SASSERT(ty->get_num_parameters() >= 1);
        return ty->get_num_parameters() - 1;
    }

    sort* util::get_datatype_parameter_sort(sort * ty, unsigned idx) {
        SASSERT(idx < get_datatype_num_parameter_sorts(ty));
        return to_sort(ty->get_parameter(idx+1).get_ast());
    }

    param_size::size* util::get_sort_size(sort_ref_vector const& params, sort* s) {
        if (params.empty()) {
            return param_size::size::mk_offset(s->get_num_elements());
        }
        if (is_datatype(s)) {
            param_size::size* sz;
            obj_map<sort, param_size::size*> S;
            unsigned n = get_datatype_num_parameter_sorts(s);
            for (unsigned i = 0; i < n; ++i) {
                sort* ps = get_datatype_parameter_sort(s, i);
                sz = get_sort_size(params, ps);
                sz->inc_ref();
                S.insert(ps, sz); 
            }
            def & d = get_def(s->get_name());
            sz = d.sort_size()->subst(S);
            for (auto & kv : S) {
                kv.m_value->dec_ref();
            }
            return sz;
        }
        array_util autil(m);
        if (autil.is_array(s)) {
            unsigned n = get_array_arity(s);
            ptr_vector<param_size::size> szs;
            for (unsigned i = 0; i < n; ++i) {
                szs.push_back(get_sort_size(params, get_array_domain(s, i)));
            }
            param_size::size* sz1 = param_size::size::mk_times(szs);
            param_size::size* sz2 = get_sort_size(params, get_array_range(s));
            return param_size::size::mk_power(sz2, sz1);
        }
        for (sort* p : params) {           
            if (s == p) return param_size::size::mk_param(sort_ref(s, m));
        }
        return param_size::size::mk_offset(s->get_num_elements());        
    }
    
    void util::compute_datatype_size_functions(svector<symbol> const& names) {
        map<symbol, status, symbol_hash_proc, symbol_eq_proc> already_found;
        map<symbol, param_size::size*, symbol_hash_proc, symbol_eq_proc> szs;

        svector<symbol> todo(names);
        status st;
        while (!todo.empty()) {
            symbol s = todo.back();
            if (already_found.find(s, st) && st == BLACK) {
                todo.pop_back();
                continue;
            }
            already_found.insert(s, GRAY);
            bool is_infinite = false;
            bool can_process = true;
            def& d = get_def(s);
            for (constructor const& c : d) {
                for (accessor const& a : c) {
                    sort* r = a.range();
                    if (is_datatype(r)) {
                        symbol s2 = r->get_name();
                        if (already_found.find(s2, st)) {
                            // type is infinite
                            if (st == GRAY) {
                                is_infinite = true;
                            }
                        }
                        else if (names.contains(s2)) {
                            todo.push_back(s2);
                            can_process = false;
                        }
                    }
                }
            }
            if (!can_process) {
                continue;
            }
            todo.pop_back();
            already_found.insert(s, BLACK);
            if (is_infinite) {
                d.set_sort_size(param_size::size::mk_offset(sort_size::mk_infinite()));
                continue;
            }

            ptr_vector<param_size::size> s_add;        
            for (constructor const& c : d) {
                ptr_vector<param_size::size> s_mul;
                for (accessor const& a : c) {
                    s_mul.push_back(get_sort_size(d.params(), a.range()));
                }
                s_add.push_back(param_size::size::mk_times(s_mul));
            }
            d.set_sort_size(param_size::size::mk_plus(s_add));
        }
    }
    

    /**
       \brief Return true if the inductive datatype is well-founded.
       Pre-condition: The given argument constains the parameters of an inductive datatype.
    */
    bool util::is_well_founded(unsigned num_types, sort* const* sorts) {
        buffer<bool> well_founded(num_types, false);
        obj_map<sort, unsigned> sort2id;
        for (unsigned i = 0; i < num_types; ++i) {
            sort2id.insert(sorts[i], i);
        }
        unsigned num_well_founded = 0, id = 0;
        bool changed;
        do {
            changed = false;
            for (unsigned tid = 0; tid < num_types; tid++) {
                if (well_founded[tid]) {
                    continue;
                }
                sort* s = sorts[tid];
                def const& d = get_def(s);
                for (constructor const& c : d) {
                    bool found_nonwf = false;
                    for (accessor const& a : c) {
                        if (sort2id.find(a.range(), id) && !well_founded[id]) {
                            found_nonwf = true;
                            break;
                        }
                    }
                    if (!found_nonwf) {
                        changed = true;
                        well_founded[tid] = true;
                        num_well_founded++;
                        break;
                    }
                }
            }
        } 
        while(changed && num_well_founded < num_types);
        return num_well_founded == num_types;
    }

    def const& util::get_def(sort* s) const {
        return m_plugin->get_def(s);
    }

    void util::get_subsorts(sort* s, ptr_vector<sort>& sorts) const {
        sorts.push_back(s);
        for (unsigned i = 0; i < s->get_num_parameters(); ++i) {
            parameter const& p = s->get_parameter(i);
            if (p.is_ast() && is_sort(p.get_ast())) {
                get_subsorts(to_sort(p.get_ast()), sorts);
            }
        }
    }


    util::util(ast_manager & m):
        m(m),
        m_family_id(m.mk_family_id("datatype")),
        m_asts(m),
        m_start(0) {
        m_plugin = dynamic_cast<decl::plugin*>(m.get_plugin(m_family_id));
    }

    util::~util() {
        std::for_each(m_vectors.begin(), m_vectors.end(), delete_proc<ptr_vector<func_decl> >());
    }

    ptr_vector<func_decl> const & util::get_datatype_constructors(sort * ty) {
        SASSERT(is_datatype(ty));
        ptr_vector<func_decl> * r = 0;
        if (m_datatype2constructors.find(ty, r))
            return *r;
        r = alloc(ptr_vector<func_decl>);
        m_asts.push_back(ty);
        m_vectors.push_back(r);
        m_datatype2constructors.insert(ty, r);
        def const& d = get_def(ty);
        for (constructor const& c : d) {
            func_decl_ref f = c.instantiate(ty);
            m_asts.push_back(f);
            r->push_back(f);
        }
        return *r;
    }

    ptr_vector<func_decl> const & util::get_constructor_accessors(func_decl * con) {
        SASSERT(is_constructor(con));
        ptr_vector<func_decl> * res = 0;
        if (m_constructor2accessors.find(con, res))
            return *res;
        res = alloc(ptr_vector<func_decl>);
        m_asts.push_back(con);
        m_vectors.push_back(res);
        m_constructor2accessors.insert(con, res);
        sort * datatype = con->get_range();
        def const& d = get_def(datatype);
        for (constructor const& c : d) {
            if (c.name() == con->get_name()) {
                for (accessor const& a : c) {
                    res->push_back(a.instantiate(datatype));
                }
                break;
            }
        }
        return *res;
    }

    func_decl * util::get_constructor_recognizer(func_decl * constructor) {
        SASSERT(is_constructor(constructor));
        func_decl * d = 0;
        if (m_constructor2recognizer.find(constructor, d))
            return d;
        sort * datatype = constructor->get_range();
        parameter ps[1] = { parameter(constructor) };
        d  = m.mk_func_decl(m_family_id, OP_DT_RECOGNISER, 1, ps, 1, &datatype);
        SASSERT(d);
        m_asts.push_back(constructor);
        m_asts.push_back(d);
        m_constructor2recognizer.insert(constructor, d);
        return d;
    }

    func_decl * util::get_recognizer_constructor(func_decl * recognizer) {
        SASSERT(is_recognizer(recognizer));
        return to_func_decl(recognizer->get_parameter(0).get_ast());
    }

    bool util::is_recursive(sort * ty) {
        SASSERT(is_datatype(ty));
        bool r = false;
        if (!m_is_recursive.find(ty, r)) {
            r = is_recursive_core(ty);
            m_is_recursive.insert(ty, r);
            m_asts.push_back(ty);
        }
        return r;
    }

    bool util::is_enum_sort(sort* s) {
        if (!is_datatype(s)) {
            return false;
        }
        bool r = false;
        if (m_is_enum.find(s, r))
            return r;
        ptr_vector<func_decl> const& cnstrs = get_datatype_constructors(s);
        r = true;
        for (unsigned i = 0; r && i < cnstrs.size(); ++i) {
            r = cnstrs[i]->get_arity() == 0;
        }
        m_is_enum.insert(s, r);
        m_asts.push_back(s);
        return r;
    }

    func_decl * util::get_accessor_constructor(func_decl * accessor) { 
        SASSERT(is_accessor(accessor));
        func_decl * r = 0;
        if (m_accessor2constructor.find(accessor, r))
            return r;
        sort * datatype = accessor->get_domain(0);
        symbol c_id   = accessor->get_parameter(1).get_symbol();
        def const& d = get_def(datatype);
        func_decl_ref fn(m);
        for (constructor const& c : d) {
            if (c.name() == c_id) {
                fn = c.instantiate(datatype);
                break;
            }
        }
        r = fn;
        m_accessor2constructor.insert(accessor, r);
        m_asts.push_back(accessor);
        m_asts.push_back(r);
        return r;
    }


    void util::reset() {
        m_datatype2constructors.reset();
        m_datatype2nonrec_constructor.reset();
        m_constructor2accessors.reset();
        m_constructor2recognizer.reset();
        m_recognizer2constructor.reset();
        m_accessor2constructor.reset();
        m_is_recursive.reset();
        m_is_enum.reset();
        std::for_each(m_vectors.begin(), m_vectors.end(), delete_proc<ptr_vector<func_decl> >());
        m_vectors.reset();
        m_asts.reset();
        ++m_start;
    }


    /**
       \brief Return a constructor mk(T_1, ... T_n)
       where each T_i is not a datatype or it is a datatype that contains 
       a constructor that will not contain directly or indirectly an element of the given sort.
    */
    func_decl * util::get_non_rec_constructor(sort * ty) {
        SASSERT(is_datatype(ty));
        func_decl * r = 0;
        if (m_datatype2nonrec_constructor.find(ty, r))
            return r;
        r = 0;
        ptr_vector<sort> forbidden_set;
        forbidden_set.push_back(ty);
        r = get_non_rec_constructor_core(ty, forbidden_set);
        SASSERT(forbidden_set.back() == ty);
        SASSERT(r);
        m_asts.push_back(ty);
        m_asts.push_back(r);
        m_datatype2nonrec_constructor.insert(ty, r);
        return r;
    }

    /**
       \brief Return a constructor mk(T_1, ..., T_n) where
       each T_i is not a datatype or it is a datatype t not in forbidden_set,
       and get_non_rec_constructor_core(T_i, forbidden_set union { T_i })
    */
    func_decl * util::get_non_rec_constructor_core(sort * ty, ptr_vector<sort> & forbidden_set) {
        // We must select a constructor c(T_1, ..., T_n):T such that
        //   1) T_i's are not recursive
        // If there is no such constructor, then we select one that 
        //   2) each type T_i is not recursive or contains a constructor that does not depend on T
        ptr_vector<func_decl> const& constructors = get_datatype_constructors(ty);
        // step 1)
        unsigned sz = constructors.size();
        ++m_start;
        for (unsigned j = 0; j < sz; ++j) {        
            func_decl * c = constructors[(j + m_start) % sz];
            unsigned num_args = c->get_arity();
            unsigned i = 0;
            for (; i < num_args; i++) {
                sort * T_i = c->get_domain(i);
                if (is_datatype(T_i))
                    break;
            }
            if (i == num_args)
                return c;
        }
        // step 2)
        for (unsigned j = 0; j < sz; ++j) {        
            func_decl * c = constructors[(j + m_start) % sz];
            TRACE("util_bug", tout << "non_rec_constructor c: " << c->get_name() << "\n";);
            unsigned num_args = c->get_arity();
            unsigned i = 0;
            for (; i < num_args; i++) {
                sort * T_i = c->get_domain(i);
                TRACE("util_bug", tout << "c: " << c->get_name() << " i: " << i << " T_i: " << T_i->get_name() << "\n";);
                if (!is_datatype(T_i)) {
                    TRACE("util_bug", tout << "T_i is not a datatype\n";);
                    continue;
                }
                if (std::find(forbidden_set.begin(), forbidden_set.end(), T_i) != forbidden_set.end()) {
                    TRACE("util_bug", tout << "T_i is in forbidden_set\n";);
                    break;
                }
                forbidden_set.push_back(T_i);
                func_decl * nested_c = get_non_rec_constructor_core(T_i, forbidden_set);
                SASSERT(forbidden_set.back() == T_i);
                forbidden_set.pop_back();
                TRACE("util_bug", tout << "nested_c: " << nested_c->get_name() << "\n";);
                if (nested_c == 0)
                    break;
            }
            if (i == num_args)
                return c;
        }
        return 0;
    }


    /**
       \brief Two datatype sorts s1 and s2 are siblings if they were
       defined together in the same mutually recursive definition.
    */
    bool util::are_siblings(sort * s1, sort * s2) {
        if (!is_datatype(s1) || !is_datatype(s2)) {
            return s1 == s2;
        }
        else {
            return get_def(s1).id() == get_def(s2).id();
        }
    }

    void util::display_datatype(sort *s0, std::ostream& strm) {
        ast_mark mark;
        ptr_buffer<sort> todo;
        SASSERT(is_datatype(s0));
        strm << s0->get_name() << " where\n";
        todo.push_back(s0);
        mark.mark(s0, true);
        while (!todo.empty()) {
            sort* s = todo.back();
            todo.pop_back();
            strm << s->get_name() << " =\n";

            ptr_vector<func_decl> const& cnstrs = get_datatype_constructors(s);
            for (unsigned i = 0; i < cnstrs.size(); ++i) {
                func_decl* cns = cnstrs[i];
                func_decl* rec = get_constructor_recognizer(cns);
                strm << "  " << cns->get_name() << " :: " << rec->get_name() << " :: ";
                ptr_vector<func_decl> const & accs = get_constructor_accessors(cns);
                for (unsigned j = 0; j < accs.size(); ++j) {
                    func_decl* acc = accs[j];
                    sort* s1 = acc->get_range();
                    strm << "(" << acc->get_name() << ": " << s1->get_name() << ") "; 
                    if (is_datatype(s1) && are_siblings(s1, s0) && !mark.is_marked(s1)) {
                        mark.mark(s1, true);
                        todo.push_back(s1);
                    }          
                }
                strm << "\n";
            }
        }
    }
}
