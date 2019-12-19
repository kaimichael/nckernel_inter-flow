# automatically detect the git protocol
if(NOT GIT_REMOTE)
    set(GIT_REMOTE "origin")
endif()

if(NOT GIT_PROTOCOL)
    execute_process(
        COMMAND git config --get "remote.${GIT_REMOTE}.url"
        COMMAND grep -Eo "^([[:alpha:]]+://)?([[:alnum:]]+@)?"
        OUTPUT_VARIABLE GIT_PROTOCOL
        OUTPUT_STRIP_TRAILING_WHITESPACE
        )
endif(NOT GIT_PROTOCOL)

# default git protocol to HTTPS
if(NOT GIT_PROTOCOL)
    set(GIT_PROTOCOL "https://")
elseif(NOT GIT_PROTOCOL MATCHES "^[a-z]*://")
    set(GIT_PROTOCOL "ssh://${GIT_PROTOCOL}")
endif()

execute_process(
    COMMAND git describe --tags
    OUTPUT_VARIABLE GIT_TAG
    OUTPUT_STRIP_TRAILING_WHITESPACE
    )

execute_process(
    COMMAND git rev-parse --short HEAD
    OUTPUT_VARIABLE GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    )

if (GIT_TAG_EXTERNAL)
	set(GIT_HASH "${GIT_TAG_EXTERNAL}")
endif()

if (GIT_HASH_EXTERNAL)
	set(GIT_HASH "${GIT_HASH_EXTERNAL}")
endif()

