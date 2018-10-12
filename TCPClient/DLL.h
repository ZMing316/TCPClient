#pragma once

#if defined(_WIN32) &&  defined(_DLL) 
	#if !defined(IMPORT)
		#define API __declspec( dllexport )
		#define LOCAL 
	#else
		#define API __declspec( dllimport )
	#endif
#endif


#if !defined(API)
	#if defined (__GNUC__) && (__GNUC__ >= 4)
		#define API __attribute__ ((visibility ("default")))
		#define LOCAL __attribute__ ((visibility ("hidden")))
	#else
		#define API
		#define LOCAL
	#endif
#endif
