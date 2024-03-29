
/*
 * Generated by gen-usi-c
 * Unicode version 15.0.0
 *
 * Taking after Simon Schoenenberger's https://github.com/detomon/unicode-table
 */

#ifndef USI_H
#define USI_H

#define IDIO_UNICODE_VERSION	"15.0.0"

#define IDIO_USI_MAX_CP		1114112
#define IDIO_USI_PAGE_SHIFT	7
#define IDIO_USI_PAGE_MASK	((1 << 7) - 1)

typedef enum {
  IDIO_USI_FLAG_Titlecase_Letter      =        1UL,	/* Category Titlecase_Letter */
  IDIO_USI_FLAG_Letter                =        2UL,	/* Category Letter */
  IDIO_USI_FLAG_Mark                  =        4UL,	/* Category Mark */
  IDIO_USI_FLAG_Decimal_Number        =        8UL,	/* Category Decimal_Number */
  IDIO_USI_FLAG_Number                =       16UL,	/* Category Number */
  IDIO_USI_FLAG_Punctuation           =       32UL,	/* Category Punctuation */
  IDIO_USI_FLAG_Symbol                =       64UL,	/* Category Symbol */
  IDIO_USI_FLAG_Separator             =      128UL,	/* Category Separator */
  IDIO_USI_FLAG_Lowercase             =      256UL,	/* Property Lowercase */
  IDIO_USI_FLAG_Uppercase             =      512UL,	/* Property Uppercase */
  IDIO_USI_FLAG_Alphabetic            =     1024UL,	/* Property Alphabetic */
  IDIO_USI_FLAG_White_Space           =     2048UL,	/* Property White_Space */
  IDIO_USI_FLAG_ASCII_Hex_Digit       =     4096UL,	/* Property ASCII_Hex_Digit */
  IDIO_USI_FLAG_Control               =     8192UL,	/* Property Control */
  IDIO_USI_FLAG_Regional_Indicator    =    16384UL,	/* Property Regional_Indicator */
  IDIO_USI_FLAG_Extend                =    32768UL,	/* Property Extend */
  IDIO_USI_FLAG_SpacingMark           =    65536UL,	/* Property SpacingMark */
  IDIO_USI_FLAG_L                     =   131072UL,	/* Property L */
  IDIO_USI_FLAG_V                     =   262144UL,	/* Property V */
  IDIO_USI_FLAG_T                     =   524288UL,	/* Property T */
  IDIO_USI_FLAG_LV                    =  1048576UL,	/* Property LV */
  IDIO_USI_FLAG_LVT                   =  2097152UL,	/* Property LVT */
  IDIO_USI_FLAG_ZWJ                   =  4194304UL,	/* Property ZWJ */
  IDIO_USI_FLAG_Fractional_Number     =  8388608UL,	/* Fractional_Number */

} idio_USI_flag_t;

#define IDIO_USI_FLAG_COUNT 24


typedef enum {
  IDIO_USI_CATEGORY_Lu,
  IDIO_USI_CATEGORY_Ll,
  IDIO_USI_CATEGORY_Lt,
  IDIO_USI_CATEGORY_Lm,
  IDIO_USI_CATEGORY_Lo,
  IDIO_USI_CATEGORY_Mn,
  IDIO_USI_CATEGORY_Mc,
  IDIO_USI_CATEGORY_Me,
  IDIO_USI_CATEGORY_Nd,
  IDIO_USI_CATEGORY_Nl,
  IDIO_USI_CATEGORY_No,
  IDIO_USI_CATEGORY_Pc,
  IDIO_USI_CATEGORY_Pd,
  IDIO_USI_CATEGORY_Ps,
  IDIO_USI_CATEGORY_Pe,
  IDIO_USI_CATEGORY_Pi,
  IDIO_USI_CATEGORY_Pf,
  IDIO_USI_CATEGORY_Po,
  IDIO_USI_CATEGORY_Sm,
  IDIO_USI_CATEGORY_Sc,
  IDIO_USI_CATEGORY_Sk,
  IDIO_USI_CATEGORY_So,
  IDIO_USI_CATEGORY_Zs,
  IDIO_USI_CATEGORY_Zl,
  IDIO_USI_CATEGORY_Zp,
  IDIO_USI_CATEGORY_Cc,
  IDIO_USI_CATEGORY_Cf,
  IDIO_USI_CATEGORY_Cs,
  IDIO_USI_CATEGORY_Co,
  IDIO_USI_CATEGORY_Cn,

} idio_USI_Category_t;


typedef enum {
  IDIO_USI_UPPERCASE_OFFSET = 0,
  IDIO_USI_LOWERCASE_OFFSET = 1,
  IDIO_USI_TITLECASE_OFFSET = 2,
} idio_USI_Case_t;

typedef uint32_t idio_USI_codepoint_t;

typedef struct idio_USI_s {
  uint32_t flags;
  idio_USI_Category_t category;
  int32_t  cases[3];
  union {
    const int64_t dec;
    const char *frac;
  } u;
} idio_USI_t;

extern const char *idio_USI_flag_names[];

extern const char *idio_USI_Category_names[];

extern const idio_USI_t idio_USI_variants[];

extern const uint16_t idio_USI_page_index[];

extern const uint16_t idio_USI_pages[][128];

static inline const idio_USI_t *idio_USI_codepoint (idio_USI_codepoint_t cp) {
  uint16_t variant = 0;

  if (cp < IDIO_USI_MAX_CP) {
    uint16_t page = idio_USI_page_index[cp >> IDIO_USI_PAGE_SHIFT];
    variant = idio_USI_pages[page][cp & IDIO_USI_PAGE_MASK];
  }

  return &(idio_USI_variants[variant]);
}

#endif
