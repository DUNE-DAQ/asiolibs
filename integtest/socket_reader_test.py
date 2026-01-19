import pytest
import os
import re
import copy

from daqconf.utils import find_free_port
import integrationtest.data_file_checks as data_file_checks
import integrationtest.log_file_checks as log_file_checks
import integrationtest.data_classes as data_classes

pytest_plugins = "integrationtest.integrationtest_drunc"

# Values that help determine the running conditions
number_of_data_producers = 1
number_of_readout_apps = 1
run_duration = 20  # seconds

# Default values for validation parameters
expected_number_of_data_files = 1
check_for_logfile_errors = True
hostname = os.uname().nodename

bern_crt_frag_params = {
    "fragment_type_description": "CRTBern",
    "fragment_type": "CRTBern",
    "expected_fragment_count": number_of_data_producers,
    "min_size_bytes": 384,
    "max_size_bytes": 800,
}
grenoble_crt_frag_params = {
    "fragment_type_description": "CRTGrenoble",
    "fragment_type": "CRTGrenoble",
    "expected_fragment_count": number_of_data_producers,
    "min_size_bytes": 1752,
    "max_size_bytes": 3992,
}

ignored_logfile_problems = {
    "local-connection-server": [
        "errorlog: -",
    ],    
}

common_config_obj = data_classes.drunc_config()
common_config_obj.dro_map_config.n_streams = number_of_data_producers
common_config_obj.dro_map_config.n_apps = number_of_readout_apps
common_config_obj.op_env = "test"
common_config_obj.config_db = (
    os.path.dirname(__file__) + "/../../daqsystemtest/config/daqsystemtest/example-configs.data.xml"
)

onebyone_local_emu_crt_bern_conf = copy.deepcopy(common_config_obj)
onebyone_local_emu_crt_bern_conf.session = "local-socket-1x1-config"

new_port = find_free_port()
onebyone_local_emu_crt_bern_conf.config_substitutions.append(
    data_classes.attribute_substitution(
        obj_class="SocketDataSender",
        obj_id="socket_sender_crt",
        updates={"port": new_port},
    )
)
print(f"Updated the crt_bern socket_sender to use port {new_port}")

onebyone_local_emu_crt_grenoble_conf = copy.deepcopy(common_config_obj)
onebyone_local_emu_crt_grenoble_conf.session = "local-socket-1x1-config"

new_port = find_free_port()
onebyone_local_emu_crt_grenoble_conf.config_substitutions.append(
    data_classes.attribute_substitution(
        obj_class="SocketDataSender",
        obj_id="socket_sender_crt",
        updates={"port": new_port},
    )
)
print(f"Updated the crt_grenoble socket_sender to use port {new_port}")
onebyone_local_emu_crt_grenoble_conf.config_substitutions.append(
    data_classes.list_element_substitution(
        obj_class="CRTReaderApplication",
        obj_id="crt-data-source-01",
        rel_name="queue_rules",
        list_index=0,
        replacement_object_class="QueueConnectionRule",
        replacement_object_id="crt-grenoble-raw-data-rule"
    )
)
onebyone_local_emu_crt_grenoble_conf.config_substitutions.append(
    data_classes.list_element_substitution(
        obj_class="ReadoutApplication",
        obj_id="socket-ru-01",
        rel_name="queue_rules",
        list_index=1,
        replacement_object_class="QueueConnectionRule",
        replacement_object_id="crt-grenoble-callback-raw-data-rule"
    )
)
onebyone_local_emu_crt_grenoble_conf.config_substitutions.append(
    data_classes.relationship_substitution(
        obj_class="ReadoutApplication",
        obj_id="socket-ru-01",
        rel_name="link_handler",
        replacement_object_class="DataHandlerConf",
        replacement_object_id="def-crt-grenoble-link-handler"
    )
)

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

    # Check that nanorc completed correctly
    assert run_nanorc.completed_process.returncode == 0 

def test_log_files(run_nanorc):
    if check_for_logfile_errors:
        # Check that there are no warnings or errors in the log files
        assert log_file_checks.logs_are_error_free(
            run_nanorc.log_files, True, True, ignored_logfile_problems
        )

def test_data_files(run_nanorc):
    fragment_check_list = []
    current_test = os.environ.get("PYTEST_CURRENT_TEST")
    if "Bern" in current_test:
        fragment_check_list.append(bern_crt_frag_params)
    elif "Grenoble" in current_test:
        fragment_check_list.append(grenoble_crt_frag_params)
    # Run some tests on the output data file
    all_ok = True
    all_ok &= len(run_nanorc.data_files) == expected_number_of_data_files
    print("") # Clear potential dot from pytest
    if all_ok:
        print(f"\N{WHITE HEAVY CHECK MARK} The correct number of raw data files was found ({expected_number_of_data_files})")
    else:
        print(f"\N{POLICE CARS REVOLVING LIGHT} An incorrect number of raw data files was found, expected {expected_number_of_data_files}, found {len(run_nanorc.data_files)} \N{POLICE CARS REVOLVING LIGHT}")

    for idx in range(len(run_nanorc.data_files)):
        data_file = data_file_checks.DataFile(run_nanorc.data_files[idx])
        for jdx in range(len(fragment_check_list)):
            all_ok &= data_file_checks.check_fragment_count(
                data_file, fragment_check_list[jdx]
            )
            all_ok &= data_file_checks.check_fragment_sizes(
                data_file, fragment_check_list[jdx]
            )

    assert all_ok
