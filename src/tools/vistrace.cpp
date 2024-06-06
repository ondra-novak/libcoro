#include <cstdint>
#include <map>

#ifndef LIBCORO_ENABLE_TRACE
#define LIBCORO_ENABLE_TRACE
#endif

#include "../coro/trace.h"
#include "getoptxx.h"


#include <variant>
#include <iostream>
#include <fstream>
#include <sstream>
#include <stack>
#include <map>
#include <set>
#include <vector>
#include <charconv>
#include <numeric>
#include <cstdlib>

#ifndef _WIN32
#include <cxxabi.h>
#endif


#include <algorithm>
struct coro_info_t {
    std::size_t slot_id = 0;
    std::string id = {};
    std::string fn_name = {};
    std::string file = {};
    unsigned int line = 0;
    std::size_t size = 0;
    std::uintptr_t addr = 0;
    std::string type = {};
    bool _destroyed = false;
    template<bool multiline>
    std::string generate_label(unsigned int label_limit) const;

    coro_info_t(std::string_view id, std::size_t slot_id):slot_id(slot_id),id(id) {};



};



using coro_ident = std::shared_ptr<coro_info_t>;

struct thread_state_t {

    struct stack_item_t {
        coro_ident id;
        bool created;
    };

    using main_stack_t = std::stack<stack_item_t>;

    main_stack_t _stack;
    coro_ident _last_active;
    std::string _tid;
    std::string generate_label(unsigned int id) const;

};

struct rel_create {
    coro_ident _target;
    bool _suspended = false;
    bool operator==(const rel_create &) const = default;
};

struct rel_destroy {
    enum type {
        t_call,
        t_suspend,
        t_return
    };
    coro_ident _target;
    type _type = t_call;
    bool operator==(const rel_destroy &) const = default;
};

struct rel_yield {
    std::string _type;
    bool operator==(const rel_yield &) const = default;
};


struct rel_await {
    std::string _type;
    std::size_t _offset = 0;
    bool operator==(const rel_await &) const = default;
};

struct rel_resume {
    coro_ident _target;
    bool operator==(const rel_resume &) const = default;
};

struct rel_return {
    coro_ident _target;
    bool operator==(const rel_return &) const = default;
};

struct rel_suspend{
    coro_ident _target;
    bool operator==(const rel_suspend &) const = default;
};

struct rel_location{
    bool operator==(const rel_location &) const {return true;}
};

struct rel_switch{
    coro_ident _target;
    bool operator==(const rel_switch &) const = default;
};
struct rel_user_log {
    std::string text;
    bool operator==(const rel_user_log &) const = default;
};
struct rel_unknown_switch {
    coro_ident _target;
    bool operator==(const rel_unknown_switch &) const = default;
};
struct rel_hline {
    std::string _text;
    bool operator==(const rel_hline &) const = default;
};
struct rel_loop {
    std::size_t count;
    bool operator==(const rel_loop &) const = default;
};

struct rel_end_loop {
    std::size_t count;
    bool operator==(const rel_end_loop &) const = default;
};

struct rel_link {
    coro_ident _target;
    bool operator==(const rel_link &) const = default;
};

template<typename T>
concept has_target = requires(T t) {
    {t._target};
};

using relation_type_t = std::variant<std::monostate,
                                     rel_create,
                                     rel_destroy,
                                     rel_yield,
                                     rel_await,
                                     rel_resume,
                                     rel_suspend,
                                     rel_switch,
                                     rel_return,
                                     rel_user_log,
                                     rel_unknown_switch,
                                     rel_hline,
                                     rel_loop,
                                     rel_end_loop,
                                     rel_link,
                                     rel_location
                                     >;


struct relation_t {
    unsigned int _thread = 0;
    coro_ident _coro;
    relation_type_t _rel_type;
    std::string _file = {};
    unsigned int _line = 0;
    bool operator==(const relation_t &other) const = default;
    bool equal_ignore_user(const relation_t &other) const {
        return _thread == other._thread
                && _coro == other._coro
                && std::visit([](const auto &a, const auto &b){
            using T = std::decay_t<decltype(a)>;
            using U = std::decay_t<decltype(b)>;
            if constexpr(std::is_same_v<T, U>) {
                if constexpr(std::is_same_v<T, rel_user_log>) {
                    return true;
                } else {
                    return a == b;
                }
            } else {
                return false;
            }
        }, _rel_type, other._rel_type);
    }
    bool has_location() const {return !_file.empty();}
};

struct addr_map_item_t {
    std::size_t size;
    coro_ident id;
};


using coro_map_t = std::map<std::string, coro_ident, std::less<> >;
using thread_map_t = std::map<unsigned int, thread_state_t>;
using relation_list_t = std::vector<relation_t>;
using coro_addr_map_t = std::map<std::uintptr_t, addr_map_item_t>;
using unresolved_links_t = std::map<std::uintptr_t, coro_ident>;

class App {
public:


    coro_map_t _coro_map;
    thread_map_t _thread_map;
    relation_list_t _relations;
    coro_addr_map_t _coro_addr_map;
    unresolved_links_t _unresolved_links;
    std::vector<coro_ident> _all_coro_idents;


    bool parse_line(std::istream &f, std::vector<std::string_view> &parts);

    void parse(std::istream &f);

    void export_uml(std::ostream &out, unsigned int label_size);

    bool filter_section(const std::string &section);
    bool filter_active();
    void filter_nevents(unsigned int n);

    template<bool included> void filter_coro(std::vector<std::string> coros);

    template<bool ignore_user>
    void detect_loops();
protected:
    std::string b1;

    coro_ident get_active_coro(unsigned int thread);
    void ensure_active_coro(unsigned int thread, coro_ident id);
    void set_name(coro_ident id, std::string_view file, unsigned int line, std::string_view fun_name);
    void set_type(coro_ident id, std::string_view type);
    void add_stack_level(unsigned int thread, coro_ident id, bool create);
    void remove_stack_level(unsigned int thread, bool suspend);
    coro_ident introduce_coro(std::string_view id);
    void create_coro(coro_ident id, std::size_t sz);
    void switch_coro(unsigned int thread, coro_ident id);
    bool suspend_expected(unsigned int thread);
    void mark_destroyed(coro_ident id);

    static std::string demangle(const std::string &txt);
    static std::string short_label_size_template(std::string txt, unsigned int size);
    void set_thread(unsigned int thread, std::string_view id);

    void filter_actors();
    template<bool ignore_user>
    bool detect_loop_cycle();


    static std::uintptr_t parse_address(std::string_view id);
    const coro_ident find_coro_by_address(std::string_view id);


    std::vector<coro_ident> resolve_link(std::string_view source, std::size_t proxy_size);
    void record_unresolved(coro_ident from_coro, std::string_view to_coro);
};

std::string wordwrap(std::string_view text, unsigned long linelen) {
    std::string out;
    std::size_t begline = 0;
    constexpr std::string_view wrap_chars = " :<>,;\\/()[]";
    std::size_t fpos = text.find_first_of(wrap_chars);
    std::size_t ppos = 0;
    while (fpos != text.npos) {
        char c= text[fpos];
        int ofs = 1;
        if (c != ' ' && c != ':' && c != ')' && c != ']') {
            ++fpos;
            ofs = 0;
        }  else if (c == ':' && fpos+1< text.size()) {
            ofs = 2;
        }
        if (begline < out.size() && (fpos - ppos)+(out.size() - begline) > linelen) {
            out.push_back('\n');
            begline = out.size();
        }
        out.append(text.substr(ppos, fpos-ppos));
        if (ofs) {
            if (begline < out.size() && ofs+(out.size() - begline) > linelen) {
                out.push_back('\n');
                begline = out.size();
            }
            if (c != ' ' || begline != out.size()) {
                out.append(text.substr(fpos, ofs));
            }
        }
        ppos = fpos+ofs;
        while (begline == out.size() && ppos < text.size() && std::isspace(text[ppos])) {
            ++ppos;
        }
        fpos = text.find_first_of(wrap_chars, ppos);
    }
    if ((out.size() - begline) + (text.length() - ppos) > linelen) {
        out.push_back('\n');
    }
    out.append(text.substr(ppos));
    return out;
}


std::string sanitise_for_line(std::string s) {
    std::string out;
    for (char c: s) {
        if (c == '\n') out.append("\\n");
        else if (c == '\\') out.append("/");
        else if (c == '"') out.append("\"");
        else if (c >= 0 && c < 32) out.push_back('.');
        else out.push_back(c);
    }
    return out;
}

std::string sanitise_for_multiline(std::string s) {
    std::string out;
    for (char c: s) {
        if ((c >= 0 && c < 32) && c != '\n') out.push_back('.');
        else if (c == '/') out.push_back('/');
        else out.push_back(c);
    }
    return out;
}

class parse_number {
public:
    parse_number(std::string_view txt, unsigned int base):_txt(txt),_base(base),_error(nullptr) {}
    parse_number(std::string_view txt, unsigned int base, std::errc &err):_txt(txt),_base(base),_error(&err) {}

    template<typename T>
    operator T() {
        T ret;
        auto [ptr, erc] = std::from_chars(_txt.data(), _txt.data()+_txt.size(), ret, _base);
        if (_error) *_error =  erc;
        _txt = _txt.substr(ptr - _txt.data());
        return ret;
    }


protected:
    std::string_view _txt;
    unsigned int _base;
    std::errc *_error;
};


std::uintptr_t App::parse_address(std::string_view id) {
    std::errc err;
    std::uintptr_t n = parse_number(id, 16, err);
    if (err == std::errc()) return n;
    std::string errmsg = "Character sequence `";
    errmsg.append(id);
    errmsg.append("` is not a valid address");
    throw std::runtime_error(errmsg);
}

const coro_ident App::find_coro_by_address(std::string_view id) {
    auto a = parse_address(id);
    auto iter =_coro_addr_map.upper_bound(a);
    if (iter == _coro_addr_map.end()) return nullptr;
    if (iter->first - a  > iter->second.size) return nullptr;
    return iter->second.id;
}

inline bool App::parse_line(std::istream &f, std::vector<std::string_view> &parts) {
    std::getline(f,b1);
    if (b1.empty()) return false;
    std::size_t sep = b1.find(coro::trace::separator);
    std::size_t psep = 0;

    parts.clear();
    while (sep != b1.npos) {
        parts.push_back(std::string_view(b1).substr(psep, sep - psep));
        psep = sep+1;
        sep = b1.find(coro::trace::separator, psep);
    }
    parts.push_back(std::string_view(b1).substr(psep));
    return true;
}

inline std::vector<coro_ident> App::resolve_link(std::string_view source, std::size_t proxy_size) {
    auto addr = parse_address(source);
    auto from = _unresolved_links.lower_bound(addr);
    auto to = _unresolved_links.upper_bound(addr+proxy_size);
    std::vector<coro_ident> targets;
    while (from != to) {
        if (!from->second->_destroyed) {
                targets.push_back(from->second);
        }
        ++from;
    }
    _unresolved_links.erase(from, to);
    return targets;
}

inline void App::record_unresolved(coro_ident from_coro, std::string_view to_coro) {
    auto addr = parse_address(to_coro);
    _unresolved_links[addr] = from_coro;
}

inline void App::parse(std::istream &f) {
    using type = coro::trace::record_type;
    try {
        std::vector<std::string_view> parts;
        while (parse_line(f, parts)) {

            relation_t rel;
            std::errc ec;
            rel._thread = parse_number(parts[0], 10, ec);
            if (ec != std::errc()) {
                throw 0;
            }
            char flag;
            if (parts.at(1).size() != 1) {
                throw 1;
            }
            flag = parts.at(1)[0];

            switch (static_cast<type>(flag)) {
                case type::thread:
                    set_thread(rel._thread, parts.at(2));
                    continue;
                case type::create:
                    rel._coro = get_active_coro(rel._thread);
                    {
                        std::size_t sz = parse_number(parts[3], 10, ec);
                        if (ec != std::errc()) {
                            throw 3;
                        }
                        create_coro(introduce_coro(parts.at(2)), sz);
                    }
                    {
                        auto coro = introduce_coro(parts.at(2));
                        rel._rel_type = rel_create{coro};
                        add_stack_level(rel._thread, coro, true);
                    }
                    break;
                case type::destroy:
                    if (!_relations.empty()
                            && std::holds_alternative<rel_suspend>(_relations.back()._rel_type)
                            && _relations.back()._coro == introduce_coro(parts.at(2))) {

                        rel._coro = _relations.back()._coro;
                        rel._rel_type = rel_destroy{
                            std::get<rel_suspend>(_relations.back()._rel_type)._target,
                            rel_destroy::t_suspend
                        };
                        _relations.pop_back();
                    } else {
                        rel._coro = get_active_coro(rel._thread);
                        auto  destroying_coro  = introduce_coro(parts.at(2));
                        if (destroying_coro == rel._coro) {
                            if (suspend_expected(rel._thread)) {
                                remove_stack_level(rel._thread, true);
                                rel._rel_type = rel_destroy{get_active_coro(rel._thread), rel_destroy::t_return};
                            } else {
                                rel._rel_type = rel_destroy{get_active_coro(rel._thread), rel_destroy::t_suspend};
                            }
                        } else {
                            rel._rel_type = rel_destroy{destroying_coro, rel_destroy::t_call};
                        }
                    }
                    mark_destroyed(introduce_coro(parts.at(2)));
                    break;
                case type::hr:
                    rel._rel_type = rel_hline{std::string(parts.at(2))};
                    break;
                case type::coroutine_type:
                    set_type(introduce_coro(parts.at(2)), parts.at(3));
                    continue;
                case type::resume_enter: {
                        rel._coro = get_active_coro(rel._thread);
                        auto coro = introduce_coro(parts.at(2));
                        rel._rel_type = rel_resume{coro};
                        add_stack_level(rel._thread, coro, false);
                    }
                    break;
                case type::resume_exit:
                    if (!_relations.empty()
                            && std::holds_alternative<rel_suspend>(_relations.back()._rel_type)
                            && std::get<rel_suspend>(_relations.back()._rel_type)._target == nullptr) {
                        //after switch to "unknown"
                        rel._coro = _relations.back()._coro;
                        rel._file = _relations.back()._file;
                        rel._line = _relations.back()._line;
                        remove_stack_level(rel._thread, false);
                        rel._rel_type = rel_return{get_active_coro(rel._thread)};
                        _relations.pop_back();

                    } else if (!_relations.empty()
                                && std::holds_alternative<rel_destroy>(_relations.back()._rel_type)
                                && std::get<rel_destroy>(_relations.back()._rel_type)._type == rel_destroy::t_suspend) {
                         rel._coro = _relations.back()._coro;
                         remove_stack_level(rel._thread, false);
                         rel._rel_type = rel_destroy{get_active_coro(rel._thread), rel_destroy::t_return};
                         _relations.pop_back();
                    }else {
                        rel._coro = get_active_coro(rel._thread);
                        remove_stack_level(rel._thread, false);
                        rel._rel_type = rel_return{get_active_coro(rel._thread)};
                    }
                    break;
                case type::sym_switch:
                    ensure_active_coro(rel._thread, introduce_coro(parts.at(2)));
                    if (parts.size() == 7) {
                        unsigned int ln = parse_number(parts.at(5), 10, ec);
                        if (ec != std::errc()) throw 6;
                        set_name(introduce_coro(parts.at(2)), parts.at(4), ln, parts.at(6));
                        rel._file = parts.at(4);
                        rel._line = ln;
                    }
                    rel._coro = get_active_coro(rel._thread);
                    if (static_cast<std::uintptr_t>(parse_number(parts.at(3),16)) == 0) {
                        if (suspend_expected(rel._thread)) {
                            remove_stack_level(rel._thread, true);
                            if (!_relations.empty()
                                    && std::holds_alternative<rel_create>(_relations.back()._rel_type)) {
                                std::get<rel_create>(_relations.back()._rel_type)._suspended = true;
                                continue;
                            } else {
                                rel._rel_type = rel_suspend{get_active_coro(rel._thread)};
                            }
                        } else {
                            rel._rel_type = rel_location{};
                        }
                    } else if (introduce_coro(parts.at(3)) == rel._coro) {
                        continue;
                    } else {
                        switch_coro(rel._thread, introduce_coro(parts.at(3)));
                        rel._rel_type = rel_switch{get_active_coro(rel._thread)};
                    }
                    break;
                case type::user_report:
                    rel._coro = get_active_coro(rel._thread);
                    rel._rel_type = rel_user_log{
                        std::accumulate(parts.begin()+3, parts.end(), std::string(parts.at(2)),
                                [](std::string &&acc, std::string_view str){
                            acc.push_back(coro::trace::separator);
                            acc.append(str);
                            return std::move(acc);
                        })
                    };
                    break;
                case type::awaits_on:
                    ensure_active_coro(rel._thread, introduce_coro(parts.at(2)));
                    rel._coro = get_active_coro(rel._thread);
                    rel._rel_type = rel_await{demangle(std::string(parts.at(3)))};
                    break;
                case type::yield:
                    ensure_active_coro(rel._thread, introduce_coro(parts.at(2)));
                    rel._coro = get_active_coro(rel._thread);
                    rel._rel_type = rel_yield{demangle(std::string(parts.at(3)))};
                    break;
                case type::link: {
                    std::size_t proxy_size = parse_number(parts.at(4), 10, ec);
                    if (ec != std::errc()) throw 4;

                    auto to_coro = find_coro_by_address(parts.at(3));
                    bool is_sync = to_coro != nullptr || static_cast<std::uintptr_t>(parse_number(parts.at(3), 16)) == 0;
                    if (is_sync) to_coro = get_active_coro(rel._thread);
                    bool to_unknown = to_coro == nullptr && !is_sync;

                    if (proxy_size) {
                        auto trg = resolve_link(parts.at(2), proxy_size);
                        if (!to_unknown) {
                            for (auto &t: trg) {
                                _relations.push_back({rel._thread,t,rel_link{to_coro}});
                            }
                        } else {
                            for (auto &t: trg) {
                                record_unresolved(t, parts.at(3));
                            }
                        }
                    }

                    auto from_coro = find_coro_by_address(parts.at(2));
                    if (from_coro != nullptr && to_unknown && proxy_size == 0) {
                        record_unresolved(from_coro, parts.at(3));
                        continue;
                    }
                    if (from_coro == nullptr || to_unknown || from_coro == to_coro) {
                        continue;
                    }
                    rel._coro = from_coro;
                    rel._rel_type = rel_link{to_coro};
                }
                break;

                default:
                    throw 1;
            }
            if (rel._rel_type.index() == 0) {
                throw 2;
            }
            _relations.push_back(rel);
        }


    } catch (int x) {
        std::string msg = "Parse error at line: `" + b1 + "` argument " + std::to_string(x);
        throw std::runtime_error(msg);
    }
}

inline coro_ident App::get_active_coro(unsigned int thread) {
    return _thread_map[thread]._last_active;
}

inline void App::switch_coro(unsigned int thread, coro_ident id) {
    auto &t = _thread_map[thread];
    if (!t._stack.empty()) {
        t._stack.top().id = id;
    }
    t._last_active = id;
}

inline bool App::suspend_expected(unsigned int thread) {
    auto &t = _thread_map[thread];
    return !t._stack.empty() && t._stack.top().created;
}

inline void App::ensure_active_coro(unsigned int thread, coro_ident id) {
    if (get_active_coro(thread) !=  id) {
       _relations.push_back(relation_t{
           thread, get_active_coro(thread), rel_unknown_switch{id}
       });
       switch_coro(thread, id);
    }
}

inline void App::set_name(coro_ident c, std::string_view file, unsigned int line,
        std::string_view fun_name) {
    c->file = std::string(file);
    c->fn_name = std::string(fun_name);
    c->line = line;

}

inline void App::set_type(coro_ident c, std::string_view type) {
    c->type = demangle(std::string(type));
}

inline void App::add_stack_level(unsigned int thread, coro_ident coro, bool created) {
    auto &thr = _thread_map[thread];
    thr._stack.push({coro, created});
    thr._last_active = thr._stack.top().id;
}

inline void App::remove_stack_level(unsigned int thread, bool suspend) {
    auto &thr = _thread_map[thread];
    if (thr._stack.empty()) {
        thr._last_active = {};
    } else {
        if (suspend && !thr._stack.top().created) return ;
        thr._stack.pop();
        if (thr._stack.empty()) {
            thr._last_active = {};
        } else {
            thr._last_active  = thr._stack.top().id;
        }
    }
}

inline coro_ident App::introduce_coro(std::string_view id) {
    std::string str_id(id);
    auto iter = _coro_map.find(str_id);
    if (iter == _coro_map.end()) {
        auto inst = std::make_shared<coro_info_t>(id, _all_coro_idents.size());
        _all_coro_idents.push_back(inst);
        inst->addr = parse_address(id);
        _coro_map.emplace(str_id, inst);
        return inst;
    } else {
        return iter->second;
    }
}

inline void App::create_coro(coro_ident id, std::size_t sz) {

    id->addr = id->addr - id->size + sz;
    id->size = sz;
    _coro_addr_map.insert({id->addr, {sz, id}});


}

inline void App::mark_destroyed(coro_ident id) {
    if (id->_destroyed) return;
    id->_destroyed = true;
    _coro_addr_map.erase(id->addr);
    _coro_map.erase(id->id);

}

inline std::string App::demangle(const std::string &txt) {
    #ifdef _WIN32
        return txt; //no demangling is needed
    #else
        std::string out;
        std::size_t len = 0;
        int status = 0;
        char *c =  abi::__cxa_demangle(txt.data(), nullptr, &len, &status);
        if (status == 0) {
            out.append(c);
            std::free(c);
        } else {
            out = txt;
        }
        return out;
    #endif
}




inline std::string thread_state_t::generate_label(unsigned int id) const {
    std::string z = _tid;
    for (char &c: z) {
        if (c == '"') c = '`';
        if (c >= 0 && c < 32) c = '.';
    }
    return "thread #" + std::to_string(id) + "\\n" + z;
}




static std::string_view strip_path(std::string_view where, unsigned int label_limit) {
    while (where.length() > label_limit) {
        auto pos = where.find_first_of("\\/");
        if (pos == where.npos) return where;
        where = where.substr(pos+1);
    }
    return where;
}


template<bool multiline>
inline std::string coro_info_t::generate_label(unsigned int label_limit) const {
    std::string n = id;
    if (!file.empty()) {
        n.append("\n").append(wordwrap(file, label_limit));
        n.push_back(':');
        n.append(std::to_string(line));
    }
    if (multiline) return sanitise_for_multiline(n);
    else return sanitise_for_line(n);
}


void App::export_uml(std::ostream &out, unsigned int label_size) {
    out << "@startuml\n"
            "skinparam NoteTextAlignment center\n";

    for (const auto &[id,info]: _thread_map) {
        out << "control \"" << info.generate_label(id) << "\" as T" << id << "\n";
        out << "activate T" << id << "\n";
    }

    std::set<coro_ident> created_actors;


    for (auto &r: _relations) {
          std::visit([&](const auto &rel){
              using T = std::decay_t<decltype(rel)>;
              if constexpr(std::is_same_v<T, rel_create>) {
                  created_actors.insert(rel._target);
              }
          },r._rel_type);
    }

    for (auto ident: _all_coro_idents) if (ident) {
        if (created_actors.find(ident) == created_actors.end()) {
            out << "participant C" << ident->slot_id << "[\n"
                    << ident->generate_label<true>(20) << "\n"
                    << "----\n"
                    << wordwrap(ident->type, label_size) << "\n"
                    << "]\n";
        }
    }
    auto node_name = [](unsigned int thread, coro_ident coro) {
        if (coro == nullptr) return "T"+std::to_string(thread);
        else return "C"+std::to_string(coro->slot_id);
    };


    std::map<coro_ident, std::string> _coro_suspend_notes;
    std::multimap<coro_ident, coro_ident> _coro_suspend_links;
    std::multimap<unsigned int, coro_ident> _coro_suspend_thread_links;

    auto add_note = [&](const coro_ident &coro, const std::string &note) {
        _coro_suspend_notes[coro] = note;
    };
    auto add_link = [&](unsigned int thread, const coro_ident &coro, const coro_ident &link_coro) {
        if (coro == nullptr) _coro_suspend_thread_links.insert({thread, link_coro});
        else _coro_suspend_links.insert({coro,link_coro});
    };
    auto flush_note = [&](const coro_ident &coro) {
        auto p = _coro_suspend_links.equal_range(coro);
        bool join = false;
        for (auto iter = p.first; iter != p.second; ++iter) {
            if (join) {
                out << "& ";
            }
            out << "C" << coro->slot_id << " o<--o " << "C" << iter->second->slot_id << " : awaiting " << std::endl;
            join = true;
        }
        _coro_suspend_links.erase(p.first, p.second);
        auto iter = _coro_suspend_notes.find(coro);
        if (iter != _coro_suspend_notes.end()) {
            out << "rnote over C" << coro->slot_id << " #CDCDCD : " << iter->second << std::endl;
            _coro_suspend_notes.erase(iter);
        }
    };

    auto flush_thread_notes = [&](unsigned int thread){
        auto p = _coro_suspend_thread_links.equal_range(thread);
        bool join = false;
        for (auto iter = p.first; iter != p.second; ++iter) {
            if (join) {
                out << "& ";
            }
            out << "T" << thread << " o<--o " << "C" << iter->second->slot_id << " : blocking " << std::endl;
            join = true;
        }
        _coro_suspend_thread_links.erase(p.first, p.second);
    };


    for (auto &r: _relations) {

        if (r.has_location()) {
            add_note(r._coro, sanitise_for_line(std::string(strip_path(r._file,label_size)) + ":" + std::to_string(r._line)));
        }


          std::visit([&](const auto &rel){
              using T = std::decay_t<decltype(rel)>;
              if constexpr(std::is_same_v<T, rel_create>) {
                  const auto &info = *rel._target;
                  out << "create participant \"" << info.template generate_label<false>(20) << "\" as C" << rel._target->slot_id << "\n";
                  out << node_name(r._thread, r._coro) << "->" << node_name(r._thread, rel._target) << ": create\n";
                  if (!rel._suspended) {
                      out << "activate " << node_name(r._thread, rel._target) << "\n";
                  }
                  if (!info.fn_name.empty()) {
                      out << "note right : " << sanitise_for_line(wordwrap(info.fn_name, label_size)) << "\n";
                  } else if (!info.type.empty()) {
                      out << "note right : " << sanitise_for_line(wordwrap(info.type, label_size)) << "\n";
                  }
              } else if constexpr(std::is_same_v<T, rel_destroy>) {
                  switch (rel._type) {
                      default:
                      case rel_destroy::t_call:
                          out << node_name(r._thread, r._coro) << "->"  << node_name(r._thread, rel._target) << " !! : destroy \n";
                          break;
                      case rel_destroy::t_suspend:
                          out << node_name(r._thread, r._coro) << "->"  << node_name(r._thread, rel._target) << " : destroy and return \n";
                          out << "destroy " << node_name(r._thread, r._coro) << "\n";
                          break;
                      case rel_destroy::t_return:
                          out << node_name(r._thread, rel._target) << "<-" <<  node_name(r._thread, r._coro) <<  " : destroy and return \n";
                          out << "destroy " << node_name(r._thread, r._coro) << " \n";
                          break;
                  }
              } else if constexpr(std::is_same_v<T, rel_hline>) {
                  out << "== ";
                  for (char c: rel._text) {
                      if (c == '\n') out << "\\n";
                      else if (c >= 0 && c < 32) out << '.';
                      else out << c;
                  }
                  out << " ==\n";
              } else if constexpr(std::is_same_v<T, rel_loop>) {
                  out << "loop " << rel.count << "x\n";
              } else if constexpr(std::is_same_v<T, rel_end_loop>) {
                  out << "end\n";
              } else if constexpr(std::is_same_v<T, rel_yield>) {
                  out << "hnote over " << node_name(r._thread, r._coro) << ": **co_yield**\\n" << short_label_size_template(rel._type, label_size) << "\n";
              } else if constexpr(std::is_same_v<T, rel_suspend>) {
                  out << node_name(r._thread, rel._target) << "<-" << node_name(r._thread, r._coro) << ": suspend\n";
                  out << "deactivate " << node_name(r._thread, r._coro) << "\n";
                  flush_note(r._coro);
              } else if constexpr(std::is_same_v<T, rel_resume>) {
                  out << node_name(r._thread, r._coro) << "->" << node_name(r._thread, rel._target) << ": resume\n";
                  out << "activate " << node_name(r._thread, rel._target) << "\n";
              } else if constexpr(std::is_same_v<T, rel_return>) {
                  out << node_name(r._thread, rel._target) << "<-" << node_name(r._thread, r._coro) << " : return\n";
                  if (r._coro!= nullptr) out << "deactivate " << node_name(r._thread, r._coro) << "\n";
                  flush_note(r._coro);
              } else if constexpr(std::is_same_v<T, rel_await>) {
                  out << "hnote over " << node_name(r._thread, r._coro) << ": **co_await**\\n" << sanitise_for_line(wordwrap(rel._type, label_size)) << "\n";
                  //add_note(r._coro,"**co_await**\\n" + sanitise_for_line(wordwrap(rel._type, label_size)));
              } else if constexpr(std::is_same_v<T, rel_switch>) {
                  out << node_name(r._thread, r._coro) << "->" << node_name(r._thread, rel._target) << " --++ : switch \n";
                  flush_note(r._coro);
              } else if constexpr(std::is_same_v<T, rel_link>) {
                  add_link(r._thread, rel._target, r._coro);
              } else if constexpr(std::is_same_v<T, rel_location>) {
                  //empty
              } else if constexpr(std::is_same_v<T, rel_user_log>) {
                  out << "note over " << node_name(r._thread, r._coro) << ": **output**\\n " << rel.text << "\n";
              } else if constexpr(std::is_same_v<T, rel_unknown_switch>) {
                  out << node_name(r._thread, r._coro) << "->" << node_name(r._thread, rel._target);
                  if (r._coro != nullptr) {
                     out << " --++";
                  } else {
                     out << " ++";
                  }
                  out <<" : <<unexpected>>\n";
                  flush_note(r._coro);
              } else {
                  out << "note over " << node_name(r._thread, r._coro) << ": **unknown state** " << demangle(typeid(T).name()) << "\n";
              }

          },r._rel_type);
          flush_thread_notes(r._thread);
      }


    out << "@enduml\n";
}


bool App::filter_active() {
    std::map<unsigned int, bool> not_relevant;


    _relations.erase(std::remove_if(_relations.begin(), _relations.end(),
            [&](relation_t &rel)->bool{

        bool remove = true;

        int target_del = std::visit([&](const auto &item) {
            if constexpr(has_target<decltype(item)>) {
                if (item._target == nullptr) return 0;
                return item._target->_destroyed?1:0;
            } else {
                return -1;
            }
        },  rel._rel_type);

        if (target_del != 1) {
            if (rel._coro != nullptr) {
                remove = rel._coro->_destroyed;
            } else if (target_del == 0){
                remove = false;
            } else {
                remove = not_relevant[rel._thread];
            }
        } else {
            remove = true;
        }
        not_relevant[rel._thread] = remove;
        return remove;

    }), _relations.end());

    if (_relations.empty()) return false;

    filter_actors();

    return true;
}

template<bool included>
void App::filter_coro(std::vector<std::string> coros) {

    std::map<unsigned int, bool> not_relevant;


    _relations.erase(std::remove_if(_relations.begin(), _relations.end(),
            [&](relation_t &rel)->bool{

        bool remove = true;

        int target_del = std::visit([&](const auto &item) {
            if constexpr(has_target<decltype(item)>) {
                if (item._target == nullptr) return 0;
                return (std::find(coros.begin(), coros.end(), item._target->id) == coros.end()) == included?1:0;
            } else {
                return -1;
            }
        },  rel._rel_type);

        if (target_del != 1) {
            if (rel._coro != nullptr) {
                remove = (std::find(coros.begin(), coros.end(), rel._coro->id) == coros.end()) == included;
            } else if (target_del == 0){
                remove = false;
            } else {
                remove = not_relevant[rel._thread];
            }
        } else {
            remove = true;
        }
        not_relevant[rel._thread] = remove;
        return remove;

    }), _relations.end());


    filter_actors();


}

bool App::filter_section(const std::string &section) {
    auto st = std::find_if(_relations.begin(), _relations.end(),
            [&](relation_t &rel)->bool{
        return std::holds_alternative<rel_hline>(rel._rel_type)
                && std::get<rel_hline>(rel._rel_type)._text ==section;
    });

    if (st == _relations.end()) return false;

    auto nx = st;
    std::advance(nx,1);
    auto e = std::find_if(nx, _relations.end(),[&](relation_t &rel)->bool{
        return std::holds_alternative<rel_hline>(rel._rel_type);
    });

    if (e == nx) return false;

    relation_list_t lst(st,e);
    std::swap(lst, _relations);

    filter_actors();


    return true;
}

void App::filter_nevents(unsigned int n) {
    if (n >= _relations.size()) return;
    auto beg = _relations.begin();
    auto end = beg + _relations.size() - n;
    _relations.erase(beg,end);

    filter_actors();
}

void App::filter_actors() {

    std::set<unsigned int> threads;
    std::set<coro_ident> coros;

    for (const auto &rel: _relations) {
        threads.insert(rel._thread);
        coros.insert(rel._coro);
        std::visit([&](const auto &x){
            if constexpr(has_target<decltype(x)>) {
                coros.insert(x._target);
            }
        }, rel._rel_type);
    }

    for (auto iter = _thread_map.begin(); iter != _thread_map.end();) {
        if (threads.find(iter->first) == threads.end()) {
            iter = _thread_map.erase(iter);
        } else {
            ++iter;
        }
    }
    for (auto &coro : _all_coro_idents) {
        if (coros.find(coro) == coros.end()) {
            coro = nullptr;
        }
    }

}


void App::set_thread(unsigned int thread, std::string_view id) {
    _thread_map[thread]._tid = std::string(id);
}

std::string App::short_label_size_template(std::string txt, unsigned int size) {
    while (txt.length() > size) {
        auto pos = txt.rfind('<');
        if (pos == txt.npos) break;
        auto end = txt.find('>', pos);
        if (end == txt.npos) {
            txt[pos] = '\x1E';
        } else {
            txt.replace(pos, end-pos+1, "\x1E...\x1F");
        }
    }
    for (char &c: txt) {
        if (c == '\x1E')  c = '<';
        else if (c == '\x1F')  c = '>';
    }
    return txt;
}

template<bool ignore_user>
void App::detect_loops() {
    while (detect_loop_cycle<ignore_user>());
}

template<bool ignore_user>
bool App::detect_loop_cycle() {

    auto compare = [](const relation_t &a, const relation_t &b) {
        if constexpr(ignore_user) {
            return a.equal_ignore_user(b);
        } else {
            return a == b;
        }
    };

    std::size_t max_len = _relations.size();
    std::size_t max_seq_len = max_len/2;
    for (std::size_t len = 1; len < max_seq_len; ++len) {
        for (std::size_t pos = 0; pos < (max_len-2*len); ++pos) {
            bool found_cycle = true;
            for (std::size_t i = 0; i < len; ++i) {
                if (!compare(_relations[pos+i],_relations[pos+i+len])) {
                    found_cycle = false;
                    break;
                }
            }
            if (found_cycle) {
                std::size_t count = 1;
                do {
                    ++count;
                    if (pos + (count+1)*len > max_len) break;
                    for (std::size_t i = 0; i < len; ++i) {
                        if (!compare(_relations[pos+i] ,_relations[pos+i+count*len])) {
                            found_cycle = false;
                            break;
                        }
                    }
                } while (found_cycle);

                auto iter = _relations.begin();
                std::advance(iter, pos);
                auto iter_end = iter;
                std::advance(iter_end, len);
                if (std::find_if(iter, iter_end, [](const relation_t &rel){
                    return !std::holds_alternative<rel_link>(rel._rel_type);
                }) == iter_end) {
                    pos += len*count-1;
                    break;
                }
                *iter = relation_t{
                    iter->_thread,{},rel_loop{count}
                };
                iter_end = iter;
                std::advance(iter_end,(count-1)*len);
                std::advance(iter,1);
                iter = _relations.erase(iter, iter_end);
                std::advance(iter,len);
                _relations.insert(iter,relation_t{
                    0,{},rel_end_loop{count}
                });

                return true;
            }
        }
    }
    return false;
}

void print_help() {
    std::cerr << "Usage: program [-ah][-b count][-n count][-f <file>][-o <file>][-s <sect>][-x <id>][-i <id>]\n"
            << "  -f <file> input file name (default stdin)\n"
            << "  -o <file> output file name (default stdout)\n"
            << "  -a        all coroutines (include finished)\n"
            << "  -l        detect and collapse loops\n"
            << "  -L        detect and collapse loops ignore user data\n"
            << "  -b count  short labels up to characters (default=32)\n"
            << "  -n count  process only  last <count> events\n"
            << "  -i id     include only coroutines <id> (can repeat) \n"
            << "  -x id     exclude coroutines <id> (can repeat) \n"
            << "  -s sect   process only given section \n"
            << "  -h        show help\n";
}

int main(int argc, char *argv[]) {
    int opt;
    getopt_t getopt;
    bool process_all = false;
    bool show_help = false;
    bool collapse_loops = false;
    bool collapse_loops_ignore_user = false;
    std::string input_file;
    std::string output_file;
    int max_count = -1;
    int label_size = 32;

    std::vector<std::string> included;
    std::vector<std::string> excluded;
    std::string section;

    while ((opt = getopt(argc, argv, "ahlLf:n:o:s:i:x:")) != -1) {
        switch (opt) {
            case 'a':
                process_all = true;
                break;
            case 'h':
                show_help = true;
                break;
            case 'f':
                input_file = getopt.optarg;
                break;
            case 'o':
                output_file = getopt.optarg;
                break;
            case 'l':
                collapse_loops = true;
                break;
            case 'L':
                collapse_loops_ignore_user = true;
                break;
            case 'n':
                max_count = std::atoi(getopt.optarg);
                break;
            case 'b':
                label_size = std::atoi(getopt.optarg);
                break;
            case 's':
                section = getopt.optarg;
                break;
            case 'i':
                included.push_back(getopt.optarg);
                break;
            case 'x':
                excluded.push_back(getopt.optarg);
                break;
            default:
                std::cerr << getopt.errmsg << std::endl;
                print_help();
                return 1;
        }
    }

    if (show_help) {
        print_help();
        return 0;
    }

    App app;



    if (input_file.empty()) {
        app.parse(std::cin);
    } else {
        std::ifstream f(input_file);
        if (!f) {
            std::cerr << "Failed to open: " << input_file << std::endl;
            return 1;
        }
        app.parse(f);
    }

    if (!section.empty()) {
        if (!app.filter_section(section)) {
            std::cerr << "Section not found or it is empty" << std::endl;
            return 2;
        }
    }

    if (!process_all) {
        if (!app.filter_active()) {
            std::cerr << "No active coroutines. To process whole file, specify -a" << std::endl;
            return 2;
        }
    }

    if (!included.empty()) app.filter_coro<true>(included);
    if (!excluded.empty()) app.filter_coro<false>(excluded);

    if (max_count > 0) {
        app.filter_nevents(max_count);
    }

    if (collapse_loops) {
        app.detect_loops<false>();
    }
    if (collapse_loops_ignore_user) {
        app.detect_loops<true>();
    }

    if (output_file.empty()) {
        app.export_uml(std::cout, label_size);
    } else {
        std::ofstream f(output_file, std::ios::trunc);
        if (!f) {
            std::cerr << "Failed to open: " << output_file << std::endl;
            return 1;
        }
        app.export_uml(f, label_size);
    }


    return 0;
}
