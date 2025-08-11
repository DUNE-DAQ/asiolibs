import pytest
import os
import re
import copy

import integrationtest.log_file_checks as log_file_checks
import integrationtest.data_classes as data_classes

pytest_plugins = "integrationtest.integrationtest_drunc"

# Values that help determine the running conditions
number_of_data_producers = 1
number_of_readout_apps = 1
run_duration = 20  # seconds

# Default values for validation parameters
check_for_logfile_errors = True
hostname = os.uname().nodename

ignored_logfile_problems = {
    "local-connection-server": [
        "errorlog: -",
        r"Worker \(pid:\d+\) was sent SIGHUP"
    ],    
}

common_config_obj = data_classes.drunc_config()
common_config_obj.dro_map_config.n_streams = number_of_data_producers
common_config_obj.dro_map_config.n_apps = number_of_readout_apps
common_config_obj.config_db = (
    os.path.dirname(__file__) + "/../../daqsystemtest/config/daqsystemtest/example-configs.data.xml"
)

onebyone_local_emu_crt_bern_conf = copy.deepcopy(common_config_obj)
onebyone_local_emu_crt_bern_conf.session = "local-emu-crt-bern-1x1-config"

onebyone_local_emu_crt_grenoble_conf = copy.deepcopy(common_config_obj)
onebyone_local_emu_crt_grenoble_conf.session = "local-emu-crt-grenoble-1x1-config"

def host_is_at_ehn1(hostname):
    return re.match(r"^(np02|np04)-srv-\d{3}$", hostname) or re.match(r"^(np02|np04)-srv-\d{3}.cern.ch$", hostname)

confgen_arguments = {
    "Local Emu CRT Bern 1x1 Conf": onebyone_local_emu_crt_bern_conf,
    "Local Emu CRT Grenoble 1x1 Conf": onebyone_local_emu_crt_grenoble_conf,
}

nanorc_command_list = "boot conf".split()
nanorc_command_list += (
    "start --run-number 101 wait 5 enable-triggers wait ".split()
    + [str(run_duration)]
    + "disable-triggers wait 1 drain-dataflow wait 2 stop-trigger-sources wait 1 stop wait 2".split()
)
nanorc_command_list += "scrap terminate".split()

def test_nanorc_success(run_nanorc):
    current_test = os.environ.get("PYTEST_CURRENT_TEST")
    match_obj = re.search(r".*\[(.+)-run_nanorc0\].*", current_test)
    if match_obj:
        current_test = match_obj.group(1)
    banner_line = re.sub(".", "=", current_test)
    print(banner_line)
    print(current_test)
    print(banner_line)    

    if not host_is_at_ehn1(hostname) and "EHN1" in current_test:
        pytest.skip(
            f"This computer ({hostname}) is not at EHN1, not running EHN1 sessions"
        )

    # Check that nanorc completed correctly
    assert run_nanorc.completed_process.returncode == 0 

def test_log_files(run_nanorc):
    current_test = os.environ.get("PYTEST_CURRENT_TEST")

    if not host_is_at_ehn1(hostname) and "EHN1" in current_test:
        pytest.skip(
            f"This computer ({hostname}) is not at EHN1, not running EHN1 sessions"
        )

    if check_for_logfile_errors:
        # Check that there are no warnings or errors in the log files
        assert log_file_checks.logs_are_error_free(
            run_nanorc.log_files, True, True, ignored_logfile_problems
        )
