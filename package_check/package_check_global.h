#pragma once

#include <QtCore/qglobal.h>

#ifndef BUILD_STATIC
# if defined(PACKAGE_CHECK_LIB)
#  define PACKAGE_CHECK_EXPORT Q_DECL_EXPORT
# else
#  define PACKAGE_CHECK_EXPORT Q_DECL_IMPORT
# endif
#else
# define PACKAGE_CHECK_EXPORT
#endif
