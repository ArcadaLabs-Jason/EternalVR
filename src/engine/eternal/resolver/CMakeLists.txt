evr_add_module(evr_resolver
    SOURCES
        pattern.cpp
        pe_image.cpp
        rip_xref.cpp
        string_search.cpp
    DEPENDS
        evr_common)
