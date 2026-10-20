#include "nixinit.h"

void nixinit_start(const nixinit_services_t* services) {
if (!services) return;
    if (services->show_status) services->show_status("Starting Desktop...", 100);
    if (services->show_status) services->show_status("Initializing IDT...", 5);
    if (services->initialize_interrupts) services->initialize_interrupts();
    if (services->show_status) services->show_status("Configuring PIT...", 15);
    if (services->initialize_timer) services->initialize_timer();
    if (services->show_status) services->show_status("Initializing ACPI...", 20);
    if (services->initialize_acpi) services->initialize_acpi();
    if (services->show_status) services->show_status("Detecting Network Adapter...", 25);
    if (services->initialize_network_device) services->initialize_network_device();
    if (services->show_status) services->show_status("Configuring IPv4", 35);
    if (services->initialize_network_stack) services->initialize_network_stack();
    if (services->show_status) services->show_status("Requesting DHCP Lease...", 50);
    if (services->configure_network) services->configure_network();
    if (services->show_status) services->show_status("Mounting SFS Filesystem...", 65);
    if (services->mount_filesystem) services->mount_filesystem();
    if (services->show_status) services->show_status("Initializing Tasking...", 75);
    if (services->initialize_tasking) services->initialize_tasking();
    if (services->show_status) services->show_status("Loading Built-in Applications...", 85);
    if (services->initialize_system_apps) services->initialize_system_apps();
    if (services->show_status) services->show_status("Initializing PS/2 Mouse...", 95);
    if (services->initialize_input) services->initialize_input();
    if (services->finish_boot) services->finish_boot();
}
