/*
 * Command implementations for thermo CLI.
 * Implements: list, get, set, init-config commands.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
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
extern void print_with_color(const char *color_name, const char *format, ...);

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
            print_with_color("yellow", "No MCC 134 boards detected.");
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
        {0, 0, 0, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "a:c:t:sDCTAJij", long_options, NULL)) != -1) {
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
    
    cJSON *output_json = NULL;
    if (json_output) {
        output_json = cJSON_CreateObject();
        cJSON_AddNumberToObject(output_json, "ADDRESS", address);
    }
    
    /* Open board for all operations that need it */
    int need_board = get_serial || get_cal_date || get_cal_coeffs || get_interval ||
                     get_temp || get_adc || get_cjc;
    
    if (need_board) {
        if (thermo_open(address) != THERMO_SUCCESS) {
            fprintf(stderr, "Error opening board at address %d\n", address);
            if (json_output) cJSON_Delete(output_json);
            return 1;
        }
    }
    
    /* Get serial number */
    if (get_serial) {
        char serial[16];
        if (thermo_get_serial(address, serial, sizeof(serial)) == THERMO_SUCCESS) {
            if (json_output) {
                cJSON_AddStringToObject(output_json, "SERIAL", serial);
            } else {
                print_with_color("green", "Serial Number (Addr %d): %s", address, serial);
            }
        }
    }
    
    /* Get calibration date */
    if (get_cal_date) {
        char date[16];
        if (thermo_get_calibration_date(address, date, sizeof(date)) == THERMO_SUCCESS) {
            if (json_output) {
                if (!cJSON_GetObjectItem(output_json, "CALIBRATION")) {
                    cJSON_AddObjectToObject(output_json, "CALIBRATION");
                }
                cJSON_AddStringToObject(cJSON_GetObjectItem(output_json, "CALIBRATION"), "DATE", date);
            } else {
                print_with_color("magenta", "Calibration Date (Addr %d): %s", address, date);
            }
        }
    }
    
    /* Get calibration coefficients */
    if (get_cal_coeffs) {
        CalibrationInfo cal;
        if (thermo_get_calibration_coeffs(address, channel, &cal) == THERMO_SUCCESS) {
            if (json_output) {
                if (!cJSON_GetObjectItem(output_json, "CHANNEL")) {
                    cJSON_AddNumberToObject(output_json, "CHANNEL", channel);
                }
                if (!cJSON_GetObjectItem(output_json, "CALIBRATION")) {
                    cJSON_AddObjectToObject(output_json, "CALIBRATION");
                }
                cJSON *cal_obj = cJSON_GetObjectItem(output_json, "CALIBRATION");
                cJSON_AddNumberToObject(cal_obj, "SLOPE", cal.slope);
                cJSON_AddNumberToObject(cal_obj, "OFFSET", cal.offset);
            } else {
                printf("Calibration Coefficients (Addr %d Ch %d):\n", address, channel);
                printf("  Slope:  %.6f\n", cal.slope);
                printf("  Offset: %.6f\n", cal.offset);
            }
        }
    }
    
    /* Get update interval */
    if (get_interval) {
        uint8_t interval;
        if (thermo_get_update_interval(address, &interval) == THERMO_SUCCESS) {
            if (json_output) {
                cJSON_AddNumberToObject(output_json, "UPDATE_INTERVAL", interval);
            } else {
                print_with_color("yellow", "Update Interval (Addr %d): %d seconds", address, interval);
            }
        }
    }
    
    /* Read temperature, ADC, or CJC */
    if (get_temp || get_adc || get_cjc) {
        /* Set TC type if we're reading temp or ADC */
        if (get_temp || get_adc) {
            if (thermo_set_tc_type(address, channel, tc_type) != THERMO_SUCCESS) {
                fprintf(stderr, "Error setting TC type\n");
                thermo_close(address);
                if (json_output) cJSON_Delete(output_json);
                return 1;
            }
            /* Wait for readings to stabilize after setting TC type */
            thermo_wait_for_readings();
        }
        
        /* Get temperature */
        if (get_temp) {
            double temp;
            if (thermo_read_temp(address, channel, &temp) == THERMO_SUCCESS) {
                if (json_output) {
                    if (!cJSON_GetObjectItem(output_json, "CHANNEL")) {
                        cJSON_AddNumberToObject(output_json, "CHANNEL", channel);
                    }
                    cJSON_AddNumberToObject(output_json, "TEMPERATURE", temp);
                } else {
                    char *temp_str = format_temperature(temp);
                    print_with_color("red", "Temperature (Addr %d Ch %d): %s °C", address, channel, temp_str);
                }
            }
        }
        
        /* Get ADC voltage */
        if (get_adc) {
            double adc;
            if (thermo_read_adc(address, channel, &adc) == THERMO_SUCCESS) {
                if (json_output) {
                    if (!cJSON_GetObjectItem(output_json, "CHANNEL")) {
                        cJSON_AddNumberToObject(output_json, "CHANNEL", channel);
                    }
                    cJSON_AddNumberToObject(output_json, "ADC", adc);
                } else {
                    print_with_color("green", "ADC (Addr %d Ch %d): %.6f V", address, channel, adc);
                }
            }
        }
        
        /* Get CJC temperature */
        if (get_cjc) {
            double cjc;
            if (thermo_read_cjc(address, channel, &cjc) == THERMO_SUCCESS) {
                if (json_output) {
                    cJSON_AddNumberToObject(output_json, "CJC", cjc);
                } else {
                    print_with_color("blue", "CJC (Addr %d): %.2f °C", address, cjc);
                }
            }
        }
    }
    
    /* Close board when done */
    if (need_board) {
        thermo_close(address);
    }
    
    if (json_output) {
        char *json_str = cJSON_Print(output_json);
        printf("%s\n", json_str);
        free(json_str);
        cJSON_Delete(output_json);
    }
    
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
            print_with_color("green", "Calibration Coefficients (Addr %d Ch %d) set to:", address, channel);
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
            print_with_color("yellow", "Update Interval (Addr %d) set to: %d seconds", address, update_interval);
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
        print_with_color("green", "Created example config: %s", output_path);
        return 0;
    } else {
        fprintf(stderr, "Error creating config file\n");
        return 1;
    }
}
