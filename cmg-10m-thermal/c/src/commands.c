/*
 * Command implementations for thermo CLI.
 * Implements: list, get, set, init-config commands.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <daqhats/daqhats.h>
#include "thermo.h"
#include "../vendor/cJSON.h"

/* External table functions from utils.c */
typedef struct Table Table;
extern Table* table_create(int num_cols);
extern void table_set_header(Table *table, int col, const char *header);
extern void table_add_row(Table *table, char **row_data);
extern void table_print(Table *table, const char *title);
extern void table_free(Table *table);

/* Data structure for holding all readings */
typedef struct {
    int address;
    int channel;
    
    /* Flags for what data is available */
    int has_serial;
    int has_cal_date;
    int has_cal_coeffs;
    int has_temp;
    int has_adc;
    int has_cjc;
    int has_interval;
    
    /* Data values */
    char serial[16];
    char cal_date[16];
    CalibrationInfo cal_coeffs;
    double temperature;
    double adc_voltage;
    double cjc_temp;
    uint8_t update_interval;
} ThermoData;

/* Initialize ThermoData structure */
static void thermo_data_init(ThermoData *data, int address, int channel) {
    memset(data, 0, sizeof(ThermoData));
    data->address = address;
    data->channel = channel;
}

/* Collect data from the board based on requested flags */
static int thermo_data_collect(ThermoData *data, int get_serial, int get_cal_date, 
                                int get_cal_coeffs, int get_temp, int get_adc, 
                                int get_cjc, int get_interval, const char *tc_type) {
    int address = data->address;
    int channel = data->channel;
    
    /* Get serial number */
    if (get_serial) {
        if (thermo_get_serial(address, data->serial, sizeof(data->serial)) == THERMO_SUCCESS) {
            data->has_serial = 1;
        }
    }
    
    /* Get calibration date */
    if (get_cal_date) {
        if (thermo_get_calibration_date(address, data->cal_date, sizeof(data->cal_date)) == THERMO_SUCCESS) {
            data->has_cal_date = 1;
        }
    }
    
    /* Get calibration coefficients */
    if (get_cal_coeffs) {
        if (thermo_get_calibration_coeffs(address, channel, &data->cal_coeffs) == THERMO_SUCCESS) {
            data->has_cal_coeffs = 1;
        }
    }
    
    /* Get update interval */
    if (get_interval) {
        if (thermo_get_update_interval(address, &data->update_interval) == THERMO_SUCCESS) {
            data->has_interval = 1;
        }
    }
    
    /* Set TC type if we're reading temp or ADC */
    if (get_temp || get_adc) {
        if (thermo_set_tc_type(address, channel, tc_type) != THERMO_SUCCESS) {
            return THERMO_ERROR;
        }
        /* Wait for readings to stabilize after setting TC type */
        thermo_wait_for_readings();
    }
    
    /* Get temperature */
    if (get_temp) {
        if (thermo_read_temp(address, channel, &data->temperature) == THERMO_SUCCESS) {
            data->has_temp = 1;
        }
    }
    
    /* Get ADC voltage */
    if (get_adc) {
        if (thermo_read_adc(address, channel, &data->adc_voltage) == THERMO_SUCCESS) {
            data->has_adc = 1;
        }
    }
    
    /* Get CJC temperature */
    if (get_cjc) {
        if (thermo_read_cjc(address, channel, &data->cjc_temp) == THERMO_SUCCESS) {
            data->has_cjc = 1;
        }
    }
    
    return THERMO_SUCCESS;
}

/* Output data in JSON format */
static void thermo_data_output_json(const ThermoData *data, int include_address_channel) {
    cJSON *root = cJSON_CreateObject();
    
    if (include_address_channel) {
        cJSON_AddNumberToObject(root, "ADDRESS", data->address);
        cJSON_AddNumberToObject(root, "CHANNEL", data->channel);
    }
    
    if (data->has_serial) {
        cJSON_AddStringToObject(root, "SERIAL", data->serial);
    }
    
    if (data->has_cal_date || data->has_cal_coeffs) {
        cJSON *cal = cJSON_AddObjectToObject(root, "CALIBRATION");
        if (data->has_cal_date) {
            cJSON_AddStringToObject(cal, "DATE", data->cal_date);
        }
        if (data->has_cal_coeffs) {
            cJSON_AddNumberToObject(cal, "SLOPE", data->cal_coeffs.slope);
            cJSON_AddNumberToObject(cal, "OFFSET", data->cal_coeffs.offset);
        }
    }
    
    if (data->has_interval) {
        cJSON_AddNumberToObject(root, "UPDATE_INTERVAL", data->update_interval);
    }
    
    if (data->has_temp) {
        cJSON_AddNumberToObject(root, "TEMPERATURE", data->temperature);
    }
    
    if (data->has_adc) {
        cJSON_AddNumberToObject(root, "ADC", data->adc_voltage);
    }
    
    if (data->has_cjc) {
        cJSON_AddNumberToObject(root, "CJC", data->cjc_temp);
    }
    
    char *json_str = cJSON_PrintUnformatted(root);
    printf("%s\n", json_str);
    fflush(stdout);
    free(json_str);
    cJSON_Delete(root);
}

/* Calculate number of digits before decimal point for a double */
static int count_digits_before_decimal(double value) {
    if (value == 0.0) return 1;
    double abs_val = value < 0 ? -value : value;
    int digits = 0;
    if (abs_val >= 1.0) {
        digits = (int)log10(abs_val) + 1;
    } else {
        digits = 1; /* for "0." */
    }
    return digits;
}

/* Output data in human-readable format */
static void thermo_data_output_table(const ThermoData *data, int show_header, int clean_mode) {
    if (show_header) {
        printf("Address: %d, Channel: %d\n", data->address, data->channel);
        if (!clean_mode) {
            printf("----------------------------------------\n");
        }
    }
    
    if (clean_mode) {
        /* Clean mode - simple output without alignment */
        if (data->has_serial) {
            printf("Serial Number: %s\n", data->serial);
        }
        
        if (data->has_cal_date) {
            printf("Calibration Date: %s\n", data->cal_date);
        }
        
        if (data->has_cal_coeffs) {
            printf("Calibration Coefficients:\n");
            printf("  Slope: %.6f\n", data->cal_coeffs.slope);
            printf("  Offset: %.6f\n", data->cal_coeffs.offset);
        }
        
        if (data->has_interval) {
            printf("Update Interval: %d seconds\n", data->update_interval);
        }
        
        if (data->has_temp) {
            printf("Temperature: %.6f °C\n", data->temperature);
        }
        
        if (data->has_adc) {
            printf("ADC: %.6f V\n", data->adc_voltage);
        }
        
        if (data->has_cjc) {
            printf("CJC: %.6f °C\n", data->cjc_temp);
        }
        return;
    }
    
    /* Calculate max key length for alignment */
    int max_key_len = 0;
    const char *labels[] = {
        data->has_temp ? "Temperature:" : NULL,
        data->has_adc ? "ADC:" : NULL,
        data->has_cjc ? "CJC:" : NULL
    };
    
    for (int i = 0; i < 3; i++) {
        if (labels[i]) {
            int len = strlen(labels[i]);
            if (len > max_key_len) max_key_len = len;
        }
    }
    
    /* Calculate max digits before decimal and max unit length for alignment */
    int max_digits = 1;
    int max_unit_len = 0;
    
    if (data->has_temp) {
        int digits = count_digits_before_decimal(data->temperature);
        if (digits > max_digits) max_digits = digits;
        int unit_len = 3; /* " °C" */
        if (unit_len > max_unit_len) max_unit_len = unit_len;
    }
    
    if (data->has_adc) {
        int digits = count_digits_before_decimal(data->adc_voltage);
        if (digits > max_digits) max_digits = digits;
        int unit_len = 2; /* " V" */
        if (unit_len > max_unit_len) max_unit_len = unit_len;
    }
    
    if (data->has_cjc) {
        int digits = count_digits_before_decimal(data->cjc_temp);
        if (digits > max_digits) max_digits = digits;
        int unit_len = 3; /* " °C" */
        if (unit_len > max_unit_len) max_unit_len = unit_len;
    }
    
    /* Total width: sign(1) + digits + decimal(1) + precision(6) */
    int value_width = max_digits + 8;
    
    int has_non_float = data->has_serial || data->has_cal_date || data->has_cal_coeffs || data->has_interval;
    int has_float = data->has_temp || data->has_adc || data->has_cjc;
    
    if (data->has_serial) {
        printf("Serial Number: %s\n", data->serial);
    }
    
    if (data->has_cal_date) {
        printf("Calibration Date: %s\n", data->cal_date);
    }
    
    if (data->has_cal_coeffs) {
        printf("Calibration Coefficients:\n");
        printf("  Slope:  %.6f\n", data->cal_coeffs.slope);
        printf("  Offset: %.6f\n", data->cal_coeffs.offset);
    }
    
    if (data->has_interval) {
        printf("Update Interval: %d seconds\n", data->update_interval);
    }
    
    /* Separator between non-float and float data */
    if (has_non_float && has_float) {
        printf("----------------------------------------\n");
    }
    
    if (data->has_temp) {
        printf("%-*s %*.6f%*s\n", max_key_len, "Temperature:", value_width, data->temperature, max_unit_len, " °C");
    }
    
    if (data->has_adc) {
        printf("%-*s %*.6f%*s\n", max_key_len, "ADC:", value_width, data->adc_voltage, max_unit_len, " V");
    }
    
    if (data->has_cjc) {
        printf("%-*s %*.6f%*s\n", max_key_len, "CJC:", value_width, data->cjc_temp, max_unit_len, " °C");
    }
}

/* Command: list - List all connected MCC 134 boards */
int cmd_list(int argc, char **argv) {
    int json_output = 0;
    
    /* Parse options */
    static struct option long_options[] = {
        {"json", no_argument, 0, 'j'},
        {0, 0, 0, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "j", long_options, NULL)) != -1) {
        switch (opt) {
            case 'j':
                json_output = 1;
                break;
            default:
                fprintf(stderr, "Usage: thermo-cli list [--json]\n");
                return 1;
        }
    }
    
    struct HatInfo *boards = NULL;
    int count = 0;
    
    int result = thermo_list_boards(&boards, &count);
    if (result != THERMO_SUCCESS) {
        fprintf(stderr, "Error listing boards\n");
        return 1;
    }
    
    if (json_output) {
        cJSON *root = cJSON_CreateObject();
        cJSON *boards_array = cJSON_CreateArray();
        
        for (int i = 0; i < count; i++) {
            cJSON *board = cJSON_CreateObject();
            cJSON_AddNumberToObject(board, "address", boards[i].address);
            cJSON_AddStringToObject(board, "id", "MCC 134");
            cJSON_AddStringToObject(board, "name", boards[i].product_name);
            cJSON_AddItemToArray(boards_array, board);
        }
        
        cJSON_AddItemToObject(root, "boards", boards_array);
        char *json_str = cJSON_Print(root);
        printf("%s\n", json_str);
        free(json_str);
        cJSON_Delete(root);
    } else {
        if (count == 0) {
            printf("No MCC 134 boards detected.\n");
        } else {
            Table *table = table_create(3);
            table_set_header(table, 0, "Address");
            table_set_header(table, 1, "ID");
            table_set_header(table, 2, "Name");
            
            for (int i = 0; i < count; i++) {
                char addr_str[16];
                snprintf(addr_str, sizeof(addr_str), "%d", boards[i].address);
                
                char *row[3] = {addr_str, "MCC 134", boards[i].product_name};
                table_add_row(table, row);
            }
            
            table_print(table, "Connected MCC 134 Boards");
            table_free(table);
        }
    }
    
    if (boards) free(boards);
    return 0;
}

/* Command: get - Read data from a specific channel */
int cmd_get(int argc, char **argv) {
    int address = 0;
    int channel = 0;
    char tc_type[8] = "K";
    int json_output = 0;
    int stream_hz = 0;
    int clean_mode = 0;
    
    int get_serial = 0;
    int get_cal_date = 0;
    int get_cal_coeffs = 0;
    int get_temp = 0;
    int get_adc = 0;
    int get_cjc = 0;
    int get_interval = 0;
    
    static struct option long_options[] = {
        {"address", required_argument, 0, 'a'},
        {"channel", required_argument, 0, 'c'},
        {"tc-type", required_argument, 0, 't'},
        {"serial", no_argument, 0, 's'},
        {"cali-date", no_argument, 0, 'D'},
        {"cali-coeffs", no_argument, 0, 'C'},
        {"temp", no_argument, 0, 'T'},
        {"adc", no_argument, 0, 'A'},
        {"cjc", no_argument, 0, 'J'},
        {"update-interval", no_argument, 0, 'i'},
        {"json", no_argument, 0, 'j'},
        {"stream", required_argument, 0, 'S'},
        {"clean", no_argument, 0, 'l'},
        {0, 0, 0, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "a:c:t:sDCTAJijS:l", long_options, NULL)) != -1) {
        switch (opt) {
            case 'a': address = atoi(optarg); break;
            case 'c': channel = atoi(optarg); break;
            case 't': strncpy(tc_type, optarg, sizeof(tc_type) - 1); break;
            case 's': get_serial = 1; break;
            case 'D': get_cal_date = 1; break;
            case 'C': get_cal_coeffs = 1; break;
            case 'T': get_temp = 1; break;
            case 'A': get_adc = 1; break;
            case 'J': get_cjc = 1; break;
            case 'i': get_interval = 1; break;
            case 'j': json_output = 1; break;
            case 'S': stream_hz = atoi(optarg); break;
            case 'l': clean_mode = 1; break;
            default:
                fprintf(stderr, "Usage: thermo-cli get [OPTIONS]\n");
                return 1;
        }
    }
    
    /* Default to temperature if nothing specified */
    if (!get_serial && !get_cal_date && !get_cal_coeffs && 
        !get_temp && !get_adc && !get_cjc && !get_interval) {
        get_temp = 1;
    }
    
    /* Open board */
    if (thermo_open(address) != THERMO_SUCCESS) {
        fprintf(stderr, "Error opening board at address %d\n", address);
        return 1;
    }
    
    /* Stream mode - continuous readings */
    if (stream_hz > 0) {
        long sleep_us = 1000000 / stream_hz;
        struct timespec sleep_time;
        sleep_time.tv_sec = sleep_us / 1000000;
        sleep_time.tv_nsec = (sleep_us % 1000000) * 1000;
        
        /* Print header and static data once */
        if (!json_output) {
            printf("Address: %d, Channel: %d\n", address, channel);
            if (!clean_mode) {
                printf("----------------------------------------\n");
            }
        }
        
        /* Collect and output static data once */
        if (get_serial || get_cal_date || get_cal_coeffs || get_interval) {
            ThermoData static_data;
            thermo_data_init(&static_data, address, channel);
            
            if (thermo_data_collect(&static_data, get_serial, get_cal_date, get_cal_coeffs,
                                   0, 0, 0, get_interval, tc_type) != THERMO_SUCCESS) {
                fprintf(stderr, "Error collecting static data\n");
                thermo_close(address);
                return 1;
            }
            
            if (json_output) {
                thermo_data_output_json(&static_data, 1);
            } else {
                thermo_data_output_table(&static_data, 0, clean_mode);
                if (!clean_mode) {
                    printf("----------------------------------------\n");
                }
            }
        }
        
        /* Print streaming info in non-JSON mode */
        if (!json_output && !clean_mode) {
            printf("Streaming at %d Hz (Ctrl+C to stop)\n", stream_hz);
            printf("----------------------------------------\n");
        }
        
        /* Stream only dynamic data */
        while (1) {
            ThermoData data;
            thermo_data_init(&data, address, channel);
            
            /* Collect only dynamic readings (temp, adc, cjc) */
            if (thermo_data_collect(&data, 0, 0, 0,
                                   get_temp, get_adc, get_cjc, 0, tc_type) != THERMO_SUCCESS) {
                fprintf(stderr, "Error collecting data\n");
                thermo_close(address);
                return 1;
            }
            
            if (json_output) {
                thermo_data_output_json(&data, 0);
            } else {
                thermo_data_output_table(&data, 0, clean_mode);
            }
            
            nanosleep(&sleep_time, NULL);
        }
    } else {
        /* Single reading mode */
        ThermoData data;
        thermo_data_init(&data, address, channel);
        
        if (thermo_data_collect(&data, get_serial, get_cal_date, get_cal_coeffs,
                               get_temp, get_adc, get_cjc, get_interval, tc_type) != THERMO_SUCCESS) {
            fprintf(stderr, "Error collecting data\n");
            thermo_close(address);
            return 1;
        }
        
        if (json_output) {
            thermo_data_output_json(&data, 1);
        } else {
            thermo_data_output_table(&data, 1, clean_mode);
        }
    }
    
    thermo_close(address);
    return 0;
}

/* Command: set - Configure channel parameters */
int cmd_set(int argc, char **argv) {
    int address = 0;
    int channel = 0;
    double cal_slope = 0;
    double cal_offset = 0;
    int has_slope = 0;
    int has_offset = 0;
    int update_interval = 0;
    int has_interval = 0;
    
    static struct option long_options[] = {
        {"address", required_argument, 0, 'a'},
        {"channel", required_argument, 0, 'c'},
        {"cali-slope", required_argument, 0, 'S'},
        {"cali-offset", required_argument, 0, 'O'},
        {"update-interval", required_argument, 0, 'i'},
        {0, 0, 0, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "a:c:S:O:i:", long_options, NULL)) != -1) {
        switch (opt) {
            case 'a': address = atoi(optarg); break;
            case 'c': channel = atoi(optarg); break;
            case 'S': cal_slope = atof(optarg); has_slope = 1; break;
            case 'O': cal_offset = atof(optarg); has_offset = 1; break;
            case 'i': update_interval = atoi(optarg); has_interval = 1; break;
            default:
                fprintf(stderr, "Usage: thermo-cli set [OPTIONS]\n");
                return 1;
        }
    }
    
    /* Set calibration coefficients */
    if (has_slope || has_offset) {
        if (!has_slope || !has_offset) {
            fprintf(stderr, "Error: Both --cali-slope and --cali-offset must be provided\n");
            return 1;
        }
        
        if (thermo_open(address) != THERMO_SUCCESS) {
            fprintf(stderr, "Error opening board at address %d\n", address);
            return 1;
        }
        
        int result = thermo_set_calibration_coeffs(address, channel, cal_slope, cal_offset);
        thermo_close(address);
        
        if (result == THERMO_SUCCESS) {
            printf("Calibration Coefficients (Addr %d Ch %d) set to:\n", address, channel);
            printf("  Slope:  %.6f\n", cal_slope);
            printf("  Offset: %.6f\n", cal_offset);
        } else {
            fprintf(stderr, "Error setting calibration coefficients\n");
            return 1;
        }
    }
    
    /* Set update interval */
    if (has_interval) {
        if (thermo_open(address) != THERMO_SUCCESS) {
            fprintf(stderr, "Error opening board at address %d\n", address);
            return 1;
        }
        
        int result = thermo_set_update_interval(address, update_interval);
        thermo_close(address);
        
        if (result == THERMO_SUCCESS) {
            printf("Update Interval (Addr %d) set to: %d seconds\n", address, update_interval);
        } else {
            fprintf(stderr, "Error setting update interval\n");
            return 1;
        }
    }
    
    return 0;
}

/* Command: init-config - Generate example configuration file */
int cmd_init_config(int argc, char **argv) {
    char output_path[256] = "thermo_config.yaml";
    
    static struct option long_options[] = {
        {"output", required_argument, 0, 'o'},
        {0, 0, 0, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "o:", long_options, NULL)) != -1) {
        switch (opt) {
            case 'o':
                strncpy(output_path, optarg, sizeof(output_path) - 1);
                break;
            default:
                fprintf(stderr, "Usage: thermo-cli init-config [--output FILE]\n");
                return 1;
        }
    }
    
    if (config_create_example(output_path) == THERMO_SUCCESS) {
        printf("Created example config: %s\n", output_path);
        return 0;
    } else {
        fprintf(stderr, "Error creating config file\n");
        return 1;
    }
}
