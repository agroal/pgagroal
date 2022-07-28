#/usr/bin/env bash

# COMP_WORDS contains
# at index 0 the executable name (pgagroal-cli)
# at index 1 the command name (e.g., flush-all)
pgagroal_cli_completions()
{

    if [ "${#COMP_WORDS[@]}" == "2" ]; then
        # main completion: the user has specified nothing at all
        # or a single word, that is a command
        COMPREPLY=($(compgen -W "flush-idle flush-gracefully flush-all is-alive enable disable stop gracefully status details switch-to reload reset reset-server" "${COMP_WORDS[1]}"))
    fi
}


pgagroal_admin_completions()
{
    if [ "${#COMP_WORDS[@]}" == "2" ]; then
        # main completion: the user has specified nothing at all
        # or a single word, that is a command
        COMPREPLY=($(compgen -W "master-key add-user update user remove-user list-users" "${COMP_WORDS[1]}"))
    fi
}

# install the completion functions
complete -F pgagroal_cli_completions pgagroal-cli
complete -F pgagroal_admin_completions pgagroal-admin
