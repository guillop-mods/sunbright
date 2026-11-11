#pragma once

namespace Helios::Hooks
{
    // Instala el hook de actualizacion sobre RE::PlayerCharacter::VTABLE[0] (si uPlayerUpdateVfuncIdx > 0)
    // junto con el listener de entrada/tecla rapida y el despachador seguro via SFSE::GetTaskInterface().
    bool Install();
    void Shutdown();
}
