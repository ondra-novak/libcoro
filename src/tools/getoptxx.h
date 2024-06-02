
#include <cstring>
#include <cstdio>
#include <string_view>

struct getopt_t {

int     opterr = 1;             /* if error message should be printed */
int     optind = 1;             /* index into parent argv vector */
int     optopt = 0;                 /* character checked for validity */
int     optreset = 0;               /* reset getopt */
const char    *optarg = nullptr;                /* argument associated with option */
std::string errmsg;


static constexpr int BADCH = '?';
static constexpr int BADARG =':';
static constexpr std::string_view EMSG = {"",1};

const char *place = EMSG.data();

/*
 * getopt --
 *      Parse argc/argv argument vector.
 */
int operator()(int nargc, char * const nargv[], const char *ostr)
{
  const char *oli;                              /* option letter list index */



  if (optreset || !*place) {              /* update scanning pointer */
    optreset = 0;
    if (optind >= nargc || *(place = nargv[optind]) != '-') {
      place = EMSG.data();
      return (-1);
    }
    if (place[1] && *++place == '-') {      /* found "--" */
      ++optind;
      place = EMSG.data();
      return (-1);
    }
  }                                       /* option letter okay? */
  if ((optopt = (int)*place++) == (int)':' ||
    !(oli = strchr(ostr, optopt))) {
      /*
      * if the user didn't specify '-' as an option,
      * assume it means -1.
      */
      if (optopt == (int)'-')
        return (-1);
      if (!*place)
        ++optind;
      if (opterr && *ostr != ':') {
          errmsg = "illegal option -- ";
          errmsg.push_back(optopt);
      }
      return (BADCH);
  }
  if (*++oli != ':') {                    /* don't need argument */
    optarg = NULL;
    if (!*place)
      ++optind;
  }
  else {                                  /* need an argument */
    if (*place)                     /* no white space */
      optarg = place;
    else if (nargc <= ++optind) {   /* no arg */
      place = EMSG.data();
      if (*ostr == ':')
        return (BADARG);
      if (opterr) {
          errmsg = "option requires an argument --  ";
          errmsg.push_back(optopt);
      }
      return (BADCH);
    }
    else                            /* white space */
      optarg = nargv[optind];
    place = EMSG.data();
    ++optind;
  }
  return (optopt);                        /* dump back option letter */
}

};
