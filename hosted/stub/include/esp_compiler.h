/* Stub of ESP-IDF's esp_compiler.h: the branch hints and the analyzer escape
 * hatch the vendored kernel additions use. */
#pragma once

#define likely( x )      __builtin_expect( !!( x ), 1 )
#define unlikely( x )    __builtin_expect( !!( x ), 0 )

#define ESP_STATIC_ANALYZER_CHECK( x, constant )    ( x )
