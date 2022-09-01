#compdef _pgagroal_cli pgagroal-cli
#compdef _pgagroal_admin pgagroal-admin


function _pgagroal_cli()
{
    local line
    _arguments -C \
               "1: :(flush-idle flush-all flush-gracefully is-alive enable disable stop gracefully status details switch-to reload reset reset-server)" \
               "*::arg:->args"
}

function _pgagroal_admin()
{
    local line
    _arguments -C \
               "1: :(master-key add-user update-user remove-user list-users)" \
               "*::arg:->args"
}
