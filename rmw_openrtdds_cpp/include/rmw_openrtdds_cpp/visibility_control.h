#pragma once

#if defined _WIN32 || defined __CYGWIN__
  #ifdef __GNUC__
    #define RMW_OPENRTDDS_CPP_EXPORT __attribute__((dllexport))
    #define RMW_OPENRTDDS_CPP_IMPORT __attribute__((dllimport))
  #else
    #define RMW_OPENRTDDS_CPP_EXPORT __declspec(dllexport)
    #define RMW_OPENRTDDS_CPP_IMPORT __declspec(dllimport)
  #endif
  #ifdef RMW_OPENRTDDS_CPP_BUILDING_DLL
    #define RMW_OPENRTDDS_CPP_PUBLIC RMW_OPENRTDDS_CPP_EXPORT
  #else
    #define RMW_OPENRTDDS_CPP_PUBLIC RMW_OPENRTDDS_CPP_IMPORT
  #endif
#else
  #define RMW_OPENRTDDS_CPP_PUBLIC __attribute__((visibility("default")))
#endif
