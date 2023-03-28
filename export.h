/*
 * Copyright (c) 2023 Novemus Band. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 */

#pragma once

#ifdef _MSC_VER
#define TUBUS_EXPORT_DECLSPEC extern "C" __declspec(dllexport)
#define TUBUS_IMPORT_DECLSPEC extern "C" __declspec(dllimport)
#define TUBUS_CLASS_EXPORT_DECLSPEC __declspec(dllexport)
#define TUBUS_CLASS_IMPORT_DECLSPEC __declspec(dllimport)
#endif // _MSC_VER

#ifdef __GNUC__
#define TUBUS_EXPORT_DECLSPEC extern "C" __attribute__ ((visibility("default")))
#define TUBUS_IMPORT_DECLSPEC extern "C"
#define TUBUS_CLASS_EXPORT_DECLSPEC __attribute__ ((visibility("default")))
#define TUBUS_CLASS_IMPORT_DECLSPEC 
#endif


#ifdef TUBUS_EXPORTS
#define TUBUS_DECLSPEC TUBUS_EXPORT_DECLSPEC
#define TUBUS_CLASS_DECLSPEC TUBUS_CLASS_EXPORT_DECLSPEC
#else
#define TUBUS_DECLSPEC TUBUS_IMPORT_DECLSPEC
#define TUBUS_CLASS_DECLSPEC TUBUS_CLASS_IMPORT_DECLSPEC
#endif

#define TUBUS_UNIT_TESTABLE_DECLSPEC TUBUS_CLASS_DECLSPEC
