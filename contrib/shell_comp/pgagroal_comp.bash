#/usr/bin/env bash

# COMP_WORDS contains
# at index 0 the executable name (pgagroal-cli)
# at index 1 the command name (e.g., flush)
# at index 2, if required, the subcommand name (e.g., all)
pgagroal_cli_completions()
{

    if [ "${#COMP_WORDS[@]}" == "2" ]; then
        # main completion: the user has specified nothing at all
        # or a single word, that is a command
        COMPREPLY=($(compgen -W "flush ping enable disable shutdown status switch-to conf clear" "${COMP_WORDS[1]}"))
    else
        # the user has specified something else
        # subcommand required?
        case ${COMP_WORDS[1]} in
            flush)
                COMPREPLY+=($(compgen -W "gracefully idle all" "${COMP_WORDS[2]}"))
                ;;
            shutdown)
                COMPREPLY+=($(compgen -W "gracefully immediate cancel" "${COMP_WORDS[2]}"))
                ;;
            clear)
                COMPREPLY+=($(compgen -W "server prometheus" "${COMP_WORDS[2]}"))
                ;;
	    conf)
		COMPREPLY+=($(compgen -W "reload get set ls alias" "${COMP_WORDS[2]}"))
		;;
	    status)
		COMPREPLY+=($(compgen -W "details" "${COMP_WORDS[2]}"))
        esac
    fi


}



pgagroal_admin_completions()
{
    if [ "${#COMP_WORDS[@]}" == "2" ]; then
        # main completion: the user has specified nothing at all
        # or a single word, that is a command
        COMPREPLY=($(compgen -W "master-key user" "${COMP_WORDS[1]}"))
    else
        # the user has specified something else
        # subcommand required?
        case ${COMP_WORDS[1]} in
            user)
                COMPREPLY+=($(compgen -W "add del edit ls" "${COMP_WORDS[2]}"))
                ;;
        esac
    fi
}




# install the completion functions
complete -F pgagroal_cli_completions pgagroal-cli
complete -F pgagroal_admin_completions pgagroal-admin
