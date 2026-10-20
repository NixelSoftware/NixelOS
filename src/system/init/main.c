#include "nixinit.h"
#include "../../kernel/drivers/serial.h"
#include "../graphics/wm/desktop.h"

static void trace_step(const char* step) {
    debug_subsystem_print("[BOOT TRACE] ");
    debug_subsystem_print(step);
    debug_subsystem_print("\n");
}

void nixinit_start(const nixinit_services_t* services) {
    if (!services) return;
    trace_step("nixinit_start entered");
    if (services->show_status) services->show_status("Starting Desktop...", 100);
    if (services->show_status) services->show_status("Initializing IDT...", 5);
    trace_step("before IDT init");
    if (services->initialize_interrupts) services->initialize_interrupts();
    trace_step("after IDT init");
    if (services->show_status) services->show_status("Configuring PIT...", 15);
    trace_step("before PIT init");
    if (services->initialize_timer) services->initialize_timer();
    trace_step("after PIT init");
    if (services->show_status) services->show_status("Initializing ACPI...", 20);
    trace_step("before ACPI init");
    if (services->initialize_acpi) services->initialize_acpi();
    trace_step("after ACPI init");
    if (services->show_status) services->show_status("Detecting Network Adapter...", 25);
    trace_step("before network device init");
    if (services->initialize_network_device) services->initialize_network_device();
    trace_step("after network device init");
    if (services->show_status) services->show_status("Configuring IPv4", 35);
    trace_step("before network stack init");
    if (services->initialize_network_stack) services->initialize_network_stack();
    trace_step("after network stack init");
    if (services->show_status) services->show_status("Requesting DHCP Lease...", 50);
    trace_step("before network configuration");
    if (services->configure_network) services->configure_network();
    trace_step("after network configuration");
    if (services->show_status) services->show_status("Mounting SFS Filesystem...", 65);
    trace_step("before filesystem mount");
    if (services->mount_filesystem) services->mount_filesystem();
    trace_step("after filesystem mount");
    if (services->show_status) services->show_status("Initializing Tasking...", 75);
    trace_step("before tasking init");
    if (services->initialize_tasking) services->initialize_tasking();
    trace_step("after tasking init");
    if (services->show_status) services->show_status("Loading Built-in Applications...", 85);
    trace_step("before system apps init");
    if (services->initialize_system_apps) services->initialize_system_apps();
    desktop_trace_api("after system apps init");
    trace_step("after system apps init");
    desktop_trace_api("before 95% splash update");
    if (services->show_status) services->show_status("Initializing PS/2 Mouse...", 95);
    desktop_trace_api("after 95% splash update");
    trace_step("before input init");
    if (services->initialize_input) services->initialize_input();
    trace_step("after input init");
    trace_step("before finish_boot");
    if (services->finish_boot) services->finish_boot();
    trace_step("after finish_boot");
}
