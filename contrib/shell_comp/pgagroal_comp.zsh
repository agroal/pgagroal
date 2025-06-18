#compdef _pgagroal_cli pgagroal-cli
#compdef _pgagroal_admin pgagroal-admin


function _pgagroal_cli()
{
    local line
    _arguments -C \
               "1: :(flush ping enable disable shutdown status switch-to conf clear)" \
               "*::arg:->args"

    case $line[1] in
        flush)
            _pgagroal_cli_flush
            ;;
        shutdown)
            _pgagroal_cli_shutdown
            ;;
        clear)
            _pgagroal_cli_clear
            ;;
	conf)
	    _pgagroal_cli_conf
	    ;;
	status)
	    _pgagroal_cli_status
	    ;;
    esac
}

function _pgagroal_cli_flush()
{
    local line
    _arguments -C \
               "1: :(gracefully idle all)" \
               "*::arg:->args"
}

function _pgagroal_cli_conf()
{
    local line
    _arguments -C \
               "1: :(reload get set ls alias)" \
               "*::arg:->args"
}

function _pgagroal_cli_shutdown()
{
    local line
    _arguments -C \
               "1: :(gracefully immediate cancel)" \
               "*::arg:->args"
}

function _pgagroal_cli_clear()
{
    local line
    _arguments -C \
               "1: :(server prometheus)" \
               "*::arg:->args"
}


function _pgagroal_admin()
{
    local line
    _arguments -C \
               "1: :(master-key user)" \
               "*::arg:->args"

    case $line[1] in
        user)
            _pgagroal_admin_user
            ;;
    esac
}

function _pgagroal_admin_user()
{
    _arguments -C \
               "1: :(add del edit ls)" \
               "*::arg:->args"
}

function _pgagroal_cli_status()
{
    local line
    _arguments -C \
               "1: :(details)" \
               "*::arg:->args"
}
