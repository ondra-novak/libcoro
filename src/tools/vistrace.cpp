#include <variant>
#include <iostream>
#include <fstream>
#include <sstream>
#include <stack>
#include <unordered_map>
#include <vector>
#include <charconv>
#include <numeric>

#include <cxxabi.h>

#include "../coro/trace.h"

#include <algorithm>
struct coro_info_t {
    std::string id = {};
    std::string name = {};
    std::string file = {};
    std::size_t size = 0;
    unsigned int _ev_counter = 0;
    bool _destroyed = false;
    std::string generate_label() const;

};

struct thread_state_t {

    using coro_stack_t = std::deque<std::string>;
    using main_stack_t = std::stack<coro_stack_t>;

    main_stack_t _stack;
    std::string _last_active;
    unsigned int _ev_counter = 0;
    std::string generate_label(unsigned int id) const;

};

struct rel_create {
    std::string _target;
    bool _suspended = false;
};

struct rel_destroy {
    std::string _target;
    bool is_return = false;
};

struct rel_yield {
    std::string _type;
};

struct rel_await {
    std::string _type;
    std::size_t _offset = 0;
};

struct rel_resume {
    std::string _target;
};

struct rel_return {
    std::string _target;
};

struct rel_suspend{
    std::string _target;
};

struct rel_switch{
    std::string _target;
};
struct rel_user_log {
    std::string text;
};
struct rel_unknown_switch {
    std::string _target;
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
                                     rel_unknown_switch
                                     >;


struct relation_t {
    unsigned int _thread = 0;
    std::string _coro;
    relation_type_t _rel_type;
    unsigned int _src_ev_id = 0;
    unsigned int _trg_ev_id = 0;
};


using coro_map_t = std::unordered_map<std::string, coro_info_t>;
using thread_map_t = std::unordered_map<unsigned int, thread_state_t>;
using relation_list_t = std::deque<relation_t>;

class App {
public:


    coro_map_t _coro_map;
    thread_map_t _thread_map;
    relation_list_t _relations;


    bool parse_line(std::istream &f, std::vector<std::string_view> &parts);

    void parse(std::istream &f);

    void export_dot(std::ostream &out);
    void export_uml(std::ostream &out);

protected:
    std::string b1;

    const std::string &get_active_coro(unsigned int thread);
    void deactivate_coro(unsigned int thread, std::string_view id);
    void activate_coro(unsigned int thread, std::string_view id);
    void ensure_active_coro(unsigned int thread, std::string_view id);
    void set_name(std::string_view id, std::string_view file, std::string_view name);
    void add_stack_level(unsigned int thread);
    void remove_stack_level(unsigned int thread);
    coro_map_t::iterator introduce_coro(std::string_view id);
    void create_coro(std::string_view id, std::size_t sz);
    void mark_destroyed(std::string_view id);

    static std::string demangle(const std::string &txt);

    void solve_conflict(std::string id);
};




inline bool App::parse_line(std::istream &f, std::vector<std::string_view> &parts) {
    std::getline(f,b1);
    if (b1.empty()) return false;
    auto sep = b1.find(coro::trace::separator);
    auto psep = 0;

    parts.clear();
    while (sep != b1.npos) {
        parts.push_back(std::string_view(b1).substr(psep, sep - psep));
        psep = sep+1;
        sep = b1.find(coro::trace::separator, psep);
    }
    parts.push_back(std::string_view(b1).substr(psep));
    return true;
}

inline void App::parse(std::istream &f) {
    using type = coro::trace::record_type;
    try {
        std::vector<std::string_view> parts;
        while (parse_line(f, parts)) {

            relation_t rel;

            auto [ptr, ec] = std::from_chars(parts[0].begin(), parts[1].end(), rel._thread, 10);
            if (ec != std::errc()) {
                throw 0;
            }
            char flag;
            if (parts.at(1).size() != 1) {
                throw 1;
            }
            flag = parts.at(1)[0];

            switch (static_cast<type>(flag)) {
                case type::create:
                    rel._coro = get_active_coro(rel._thread);
                    {
                        std::size_t sz;
                        auto [ptr, ec] = std::from_chars(parts[3].begin(), parts[3].end(), sz, 10);
                        if (ec != std::errc()) {
                            throw 3;
                        }
                        create_coro(parts.at(2), sz);
                    }
                    rel._rel_type = rel_create{std::string(parts.at(2))};
                    activate_coro(rel._thread, parts.at(2));
                    break;
                case type::destroy:
                    if (!_relations.empty()
                            && std::holds_alternative<rel_suspend>(_relations.back()._rel_type)
                            && _relations.back()._coro == parts.at(2)) {

                        rel._coro = _relations.back()._coro;
                        rel._rel_type = rel_destroy{
                            std::get<rel_suspend>(_relations.back()._rel_type)._target,
                            true
                        };
                        _relations.pop_back();
                    } else {
                        rel._coro = get_active_coro(rel._thread);
                        std::string destroying_coro ( parts.at(2));
                        if (destroying_coro == rel._coro) {
                            deactivate_coro(rel._thread, destroying_coro);
                            rel._rel_type = rel_destroy{get_active_coro(rel._thread), true};
                        } else {
                            deactivate_coro(rel._thread, destroying_coro);
                            rel._rel_type = rel_destroy{destroying_coro, false};
                        }
                    }
                    mark_destroyed(parts.at(2));
                    break;
                case type::name:
                    set_name(parts.at(2), parts.at(3), parts.at(4));
                    continue;
                    break;
                case type::resume_enter:
                    rel._coro = get_active_coro(rel._thread);
                    rel._rel_type = rel_resume{std::string(parts.at(2))};
                    add_stack_level(rel._thread);
                    activate_coro(rel._thread,parts.at(2));
                    break;
                case type::resume_exit:
                    if (!_relations.empty()
                            && std::holds_alternative<rel_suspend>(_relations.back()._rel_type)
                            && std::get<rel_suspend>(_relations.back()._rel_type)._target.empty()) {
                        //after switch to "unknown"
                        rel._coro = _relations.back()._coro;
                        _relations.pop_back();

                    } else if (!_relations.empty()
                                && std::holds_alternative<rel_destroy>(_relations.back()._rel_type)
                                && get_active_coro(rel._thread).empty()) {
                         rel._coro = std::get<rel_destroy>(_relations.back()._rel_type)._target;
                    }else {
                        rel._coro = get_active_coro(rel._thread);
                    }
                    remove_stack_level(rel._thread);
                    rel._rel_type = rel_return{get_active_coro(rel._thread)};
                    break;
                case type::sym_switch:
                    ensure_active_coro(rel._thread, parts.at(2));
                    rel._coro = get_active_coro(rel._thread);
                    if (parts.at(3) == "0") {
                        deactivate_coro(rel._thread,rel._coro);
                        if (!_relations.empty()
                                && std::holds_alternative<rel_create>(_relations.back()._rel_type)) {
                            std::get<rel_create>(_relations.back()._rel_type)._suspended = true;
                            continue;
                        } else {
                            rel._rel_type = rel_suspend{get_active_coro(rel._thread)};
                        }
                    } else {
                        deactivate_coro(rel._thread,rel._coro);
                        activate_coro(rel._thread, parts.at(3));
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
                    ensure_active_coro(rel._thread, parts.at(2));
                    rel._coro = get_active_coro(rel._thread);
                    rel._rel_type = rel_await{demangle(std::string(parts.at(3)))};
                    break;
                case type::yield:
                    ensure_active_coro(rel._thread, parts.at(2));
                    rel._coro = get_active_coro(rel._thread);
                    rel._rel_type = rel_yield{demangle(std::string(parts.at(3)))};
                    break;
/*                default:
                    throw 1;*/
            }
            _relations.push_back(rel);
        }
        /*
        relation_list_t tmp = std::move(_relations);
        for (auto iter = tmp.begin(); iter != tmp.end(); ++iter) {
            if (std::holds_alternative<rel_suspend>(iter->_rel_type)) {
                auto nx = iter;
                ++nx;
                if (nx != tmp.end()
                     && std::get<rel_suspend>(iter->_rel_type)._target.empty()
                     && std::holds_alternative<rel_return>(nx->_rel_type)
                     && nx->_coro.empty()) {
                        _relations.push_back({
                            iter->_thread,
                            iter->_coro,
                            nx->_rel_type
                        });
                        iter = nx;
                } else {
                    _relations.push_back(std::move(*iter));
                }
            } else if (std::holds_alternative<rel_await>(iter->_rel_type)
                    && std::get<rel_await>(iter->_rel_type)._type == "coro::trace_name") {
                continue;
            } else {
                _relations.push_back(std::move(*iter));
            }
        }

        _relations.erase(std::remove_if(_relations.begin(), _relations.end(), [](const relation_t &x){
            return std::holds_alternative<rel_return>(x._rel_type) &&
                    x._coro.empty() && std::get<rel_return>(x._rel_type)._target.empty();
            }), _relations.end());
*/

    } catch (int x) {
        std::string msg = "Parse error at line: `" + b1 + "` argument " + std::to_string(x);
        throw std::runtime_error(msg);
    }
}

inline const std::string& App::get_active_coro(unsigned int thread) {
    return _thread_map[thread]._last_active;
}

inline void App::deactivate_coro(unsigned int thread, std::string_view id) {
    introduce_coro(id);
    auto &thr = _thread_map[thread];
    if (!thr._stack.empty()) {
        auto &s = thr._stack.top();
        auto f = std::find(s.begin(), s.end(), std::string(id));
        if (f != s.end()) {
            s.erase(f, s.end());
        }
        if (!s.empty()) thr._last_active = s.front();
        else thr._last_active = {};
    } else {
        thr._last_active = {};
    }
}

inline void App::activate_coro(unsigned int thread, std::string_view id) {
    introduce_coro(id);
    auto &thr = _thread_map[thread];
    if (!thr._stack.empty()) {
        auto &s = thr._stack.top();
        if (s.empty() || s.front() != id)  {
            s.push_back(std::string(id));
        }
    }
    thr._last_active = std::string(id);

}

inline void App::ensure_active_coro(unsigned int thread, std::string_view id) {
    introduce_coro(id);
    if (get_active_coro(thread) !=  id) {
       _relations.push_back(relation_t{
           thread, get_active_coro(thread), rel_unknown_switch{std::string(id)}
       });
       activate_coro(thread, id);
    }
}

inline void App::set_name(std::string_view id, std::string_view file,
        std::string_view name) {
    auto &c = _coro_map[std::string(id)];
    c.file = std::string(file);
    c.name = std::string(name);

}

inline void App::add_stack_level(unsigned int thread) {
    auto &thr = _thread_map[thread];
    thr._stack.push({});
}

inline void App::remove_stack_level(unsigned int thread) {
    auto &thr = _thread_map[thread];
    if (thr._stack.empty()) {
        thr._last_active = {};
    } else {
        thr._stack.pop();
        if (thr._stack.empty()) {
            thr._last_active = {};
        } else {
            auto &s = thr._stack.top();
            if (!s.empty()) {
                thr._last_active = s.back();
            }
        }
    }
}

inline coro_map_t::iterator App::introduce_coro(std::string_view id) {
    std::string str_id(id);
    auto iter = _coro_map.find(str_id);
    if (iter == _coro_map.end()) {
        return _coro_map.emplace(str_id, coro_info_t{str_id}).first;
    } else {
        return iter;
    }
}

inline void App::create_coro(std::string_view id, std::size_t sz) {
    auto iter = introduce_coro(id);
    if (iter->second._destroyed) {
        solve_conflict(std::string(id));
        introduce_coro(id)->second.size = sz;
    } else {
        iter->second.size = sz;
    }

}

inline void App::mark_destroyed(std::string_view id) {
    introduce_coro(id)->second._destroyed= true;
}

inline std::string App::demangle(const std::string &txt) {
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
}

inline void App::solve_conflict(std::string id) {
    int pos = 1;
    std::string new_name;
    do {
        new_name = id + "_" + std::to_string(pos);
        auto iter =_coro_map.find(new_name);
        if (iter == _coro_map.end()) break;
        ++pos;
    } while (true);

    auto citer = introduce_coro(id);
    auto niter = introduce_coro(new_name);
    niter->second = citer->second;
    niter->second.id = new_name;
    for (auto &r: _relations) {
        if (r._coro == id) r._coro = new_name;
        std::visit([&](auto &item){
            if constexpr(has_target<decltype(item)>) {
                if (item._target == id) item._target = new_name;
            }
        }, r._rel_type);
    }
}



inline void App::export_dot(std::ostream &out) {
    out << "digraph Trace {\n" ;
    std::unordered_map<unsigned int, unsigned int> thread_interactions;
    std::unordered_map<std::string_view, unsigned int> coro_interactions;

    for (auto &r: _relations) {
        auto &p = thread_interactions[r._thread];
        std::visit([&](auto &item){
            if constexpr(has_target<decltype(item)>) {
                if (item._target.empty()) {
                    ++p;
                }
                else {
                    auto &c = coro_interactions[std::string_view(item._target)];
                    ++c;
                }
            }
        }, r._rel_type);
    }


    for (auto &thr: _thread_map) {
        out << "subgraph thread_" << thr.first << "{\n"
                "cluster=true\n"
                "node [shape=point]\n"
                "label=\"" << thr.second.generate_label(thr.first) << "\"\n";
        unsigned int cnt = thread_interactions[thr.first];
        out << "t_" << thr.first << "_0" ;
        for (unsigned int i = 0; i < cnt; ++i) {
            out << "->t_" << thr.first << "_" << (i+1);
        }
        out << " [style=invis]\n";
        out << "}\n";
    }

    for (auto &coro: _coro_map) {
        out << "subgraph coro_" << coro.first << "{\n"
                "cluster=true\n"
                "node [shape=point]\n"
                "label=\"" << coro.second.generate_label() << "\"\n";
        unsigned int cnt = coro_interactions[coro.first];
        out << "c_" << coro.first << "_n0" << "\n";
        for (unsigned int i = 0; i < cnt; ++i) {
            out << "->c_" << coro.first << "_n" << (i+1);
        }
        out << " [style=invis]\n";
        out << "}\n";
    }

    auto node_name = [&](int thread, std::string_view coro, bool target) {

        if (coro.empty()) {
            auto &n = thread_interactions[thread];
            if (target) ++n;
            out << "t_" << thread <<  "_" << n;
        }
        else {
            auto &n = coro_interactions[coro];
            if (target) ++n;
            out << "c_" << coro << "_n" << n;
        }
    };

    for (auto &[k,v]: thread_interactions) v = 0;
    for (auto &[k,v]: coro_interactions) v = 0;

    unsigned int hlpcnt = 0;

    for (auto &r: _relations) {
        std::visit([&](const auto &rel){
            using T = std::decay_t<decltype(rel)>;
            if constexpr(std::is_same_v<T, rel_create>) {
                node_name(r._thread, r._coro, false);
                out << "->";
                node_name(r._thread, rel._target,true);
                out << " [label=\"create\"]\n";
            } else if constexpr(std::is_same_v<T, rel_destroy>) {
                node_name(r._thread, r._coro, false);
                out << "->";
                node_name(r._thread, rel._target,true);
                out << " [label=\"destroy\",arrowhead=tee]\n";
            } else if constexpr(std::is_same_v<T, rel_yield>) {
                hlpcnt++;
                out << "hlp_" << hlpcnt << " [label=\"yield:\\n" << rel._type << "\"]\n";
                node_name(r._thread, r._coro, false);
                out << "->";
                out << "hlp_" << hlpcnt << " [style=dotted]\n";
            } else if constexpr(std::is_same_v<T, rel_suspend>) {
                node_name(r._thread, r._coro, false);
                out << "->";
                node_name(r._thread, rel._target,true);
                out << " [label=\"suspend\"]\n";
            } else if constexpr(std::is_same_v<T, rel_resume>) {
                node_name(r._thread, r._coro, false);
                out << "->";
                node_name(r._thread, rel._target,true);
                out << " [label=\"resume\"]\n";
            } else if constexpr(std::is_same_v<T, rel_return>) {
                node_name(r._thread, r._coro, false);
                out << "->";
                node_name(r._thread, rel._target,true);
                out << " [label=\"return\"]\n";
            } else if constexpr(std::is_same_v<T, rel_switch>) {
                node_name(r._thread, r._coro, false);
                out << "->";
                node_name(r._thread, rel._target,true);
                out << " [label=\"switch\"]\n";
            }
        },r._rel_type);
    }

    out << "}\n";
}

inline std::string thread_state_t::generate_label(unsigned int id) const {
    return "thread #" + std::to_string(id);
}



int main(int argc, char **argv) {
    App app;
    if (argc == 1) {
        app.parse(std::cin);
    } else {
        std::ifstream f(argv[1]);
        if (!f) {
            std::cerr << "Failed to open: " << argv[1] << std::endl;
            return 1;
        }
        app.parse(f);
    }
    app.export_uml(std::cout);
}

static std::string strip_path(std::string_view where) {
    auto pos = where.find_last_of("\\/");
    if (pos == where.npos) return std::string(where);
    return std::string(where.substr(pos+1));
}

inline std::string coro_info_t::generate_label() const {
    if (name.empty() && file.empty()) {
        return id;
    }
    std::string n;
    if (file.empty()) {
        n = name;
    } else if (name.empty()) {
        n = strip_path(file);
    } else {
        n = name+"\\n"+strip_path(file);
    }
    for (char &c: n) if (c == '"') c = '`';
    return n;
}


void App::export_uml(std::ostream &out) {
    out << "@startuml\nskinparam NoteTextAlignment center\n";

    for (const auto &[id,info]: _thread_map) {
        out << "control \"" << info.generate_label(id) << "\" as T" << id << "\n";
        out << "activate T" << id << "\n";
    }
/*    for (const auto &[id,info]: _coro_map) {
        out << "participant \"" << info.generate_label() << "\" as C" << id << "\n";
    }*/

    auto node_name = [](unsigned int thread, std::string_view coro) {
        if (coro.empty()) return "T"+std::to_string(thread);
        else return std::string("C").append(coro);
    };


    std::unordered_map<std::string, std::string> _coro_suspend_notes;
    auto add_note = [&](const std::string &coro, const std::string &note) {
        _coro_suspend_notes[coro] = note;
    };
    auto flush_note = [&](const std::string &coro) {
        auto iter = _coro_suspend_notes.find(coro);
        if (iter != _coro_suspend_notes.end()) {
            out << "note over C" << coro << ": " << iter->second << std::endl;
            _coro_suspend_notes.erase(iter);
        }
    };


    for (auto &r: _relations) {
          std::visit([&](const auto &rel){
              using T = std::decay_t<decltype(rel)>;
              if constexpr(std::is_same_v<T, rel_create>) {
                  const auto &info = _coro_map[rel._target];
                  out << "create participant \"" << info.generate_label() << "\" as C" << rel._target << "\n";
                  out << node_name(r._thread, r._coro) << "->" << node_name(r._thread, rel._target) << ": Create\n";
                  if (!rel._suspended) {
                      out << "activate " << node_name(r._thread, rel._target) << "\n";
                  }
              } else if constexpr(std::is_same_v<T, rel_destroy>) {
                  if (rel.is_return) {
                      out << node_name(r._thread, r._coro) << "->"  << node_name(r._thread, rel._target) << " --++ \n";
                      out << "destroy " << node_name(r._thread, r._coro) << "\n";
                  } else {
                      out << node_name(r._thread, r._coro) << "->"  << node_name(r._thread, rel._target) << " !! \n";
                  }
              } else if constexpr(std::is_same_v<T, rel_yield>) {
                  out << "note over " << node_name(r._thread, r._coro) << ": **co_yield** " << rel._type << "\n";
              } else if constexpr(std::is_same_v<T, rel_suspend>) {
                  out << node_name(r._thread, r._coro) << "->" << node_name(r._thread, rel._target) << ": suspend\n";
                  out << "deactivate " << node_name(r._thread, r._coro) << "\n";
                  flush_note(r._coro);
              } else if constexpr(std::is_same_v<T, rel_resume>) {
                  out << node_name(r._thread, r._coro) << "->" << node_name(r._thread, rel._target) << ": resume\n";
                  out << "activate " << node_name(r._thread, rel._target) << "\n";
              } else if constexpr(std::is_same_v<T, rel_return>) {
                  out << node_name(r._thread, r._coro) << "->" << node_name(r._thread, rel._target) << " : return\n";
                  if (!r._coro.empty()) out << "deactivate " << node_name(r._thread, r._coro) << "\n";
                  if (r._coro.empty() && rel._target.empty()) out << "deactivate " << node_name(r._thread, "") << "\n";
                  flush_note(r._coro);
              } else if constexpr(std::is_same_v<T, rel_await>) {
                  add_note(r._coro,"**co_await**\\n" + rel._type);
              } else if constexpr(std::is_same_v<T, rel_switch>) {
                  out << node_name(r._thread, r._coro) << "->" << node_name(r._thread, rel._target) << " --++ \n";
                  flush_note(r._coro);
              } else if constexpr(std::is_same_v<T, rel_user_log>) {
                  out << "note over " << node_name(r._thread, r._coro) << ": **output**\\n " << rel.text << "\n";
              } else if constexpr(std::is_same_v<T, rel_unknown_switch>) {
                  out << node_name(r._thread, r._coro) << "->" << node_name(r._thread, rel._target);
                  if (!r._coro.empty()) {
                     out << " --++";
                  } else {
                     out << " ++";
                  }
                  out <<" : <<unexpected>>\n";
                  flush_note(r._coro);
              } else {
                  out << "note over " << node_name(r._thread, r._coro) << ": **unknown state** " << demangle(typeid(T).name()) << "\n";
              }

              /* else if constexpr(std::is_same_v<T, rel_switch>) {
                  node_name(r._thread, r._coro, false);
                  out << "->";
                  node_name(r._thread, rel._target,true);
                  out << " [label=\"switch\"]\n";
              }*/
          },r._rel_type);
      }
    out << "@enduml\n";
}
