// Stub implementations for bedmesh, load_cell, and probe symbols.
// Only compiled on x86_64 where no prebuilt ARM library is available.
#ifdef __x86_64__

#include "bedmesh/bed_mesh.h"
#include "bedmesh/load_cell.h"
#include "bedmesh/probe.h"
#include "bedmesh/load_cell_probe.h"

namespace elegoo
{
    namespace extras
    {
        int BedMesh::get_print_surface() { return 0; }
        json BedMesh::get_status(double /*eventtime*/) { return json{}; }
        std::shared_ptr<BedMesh> bed_mesh_load_config(std::shared_ptr<ConfigWrapper> /*config*/) { return nullptr; }

        double LoadCell::get_diagnostic_std_value() { return 0.0; }
        std::shared_ptr<LoadCell> load_cell_load_config(std::shared_ptr<ConfigWrapper> /*config*/) { return nullptr; }
        std::shared_ptr<LoadCell> load_cell_load_config_prefix(std::shared_ptr<ConfigWrapper> /*config*/) { return nullptr; }

        std::vector<double> run_single_probe(std::shared_ptr<PrinterProbeInterface> /*probe*/, std::shared_ptr<GCodeCommand> /*gcmd*/) { return {}; }
        std::shared_ptr<PrinterProbeInterface> probe_load_config(std::shared_ptr<ConfigWrapper> /*config*/) { return nullptr; }
        std::shared_ptr<PrinterProbeInterface> load_cell_probe_load_config(std::shared_ptr<ConfigWrapper> /*config*/) { return nullptr; }
    }
}

#endif // __x86_64__
