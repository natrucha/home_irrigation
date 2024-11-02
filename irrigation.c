/* 
Automated home irrigation based on weather data provided by CIMIS weather stations (https://cimis.water.ca.gov/Default.aspx).
Copyright (C) 2024  Natalie C. Pueyo Svoboda

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.


turn on drip/sprinkler systems based on calculations using
CIMIS provided ETo and UCANR SLIDE rules (https://ucanr.edu/sites/UrbanHort/Water_Use_of_Turfgrass_and_Landscape_Plant_Materials/SLIDE__Simplified_Irrigation_Demand_Estimation/)
to compute correct irrigation timing for the various garden locations, as well as
the BMP book SCHEDULING: KNOWING WHEN AND HOW MUCH TO IRRIGATE found July 2024 at
https://bmpbooks.com/media/Irrigation-Management-04-Scheduling-Knowing-When-and-How-Much-to-Irrigate.pdf
*/ 

#include <stdio.h>
#include <stdlib.h>

#include <jansson.h>     // json parser for C, see https://jansson.readthedocs.io/en/latest/ for documentation 
#include <curl/curl.h>   // see for examples https://curl.se/libcurl/c/example.html
#include <mosquitto.h>   // MQTT broker
#define _XOPEN_SOURCE    // required for function strptime for <time.h> 
#include <time.h>
#include <string.h>
#include <unistd.h>

// Include CMake input file, it's in the build folder so VSCode is freaking out
#include "IrrigationConfig.h"

#define BUFFER_SIZE (256 * 1024) /* 256 KB */

const char host_id[] = MQTT_HOST;
const char mqtt_ssid[] = MQTT_SSID_SECRET;
const char mqtt_password[] = MQTT_PASSWORD_SECRET;

int curr_relay_done = 0;
int curr_contrlr_done = 0;

typedef struct cimis_results {
    float Et0;
    float precip;
    int parse_errors;   // will save how many json types were incorrect
} cimis_results;

typedef struct garden_section {
    const char *name;
    float PF;
    long int LA;
    double days_since;   // number of days since last irrigation 
    float eff_irr;       // effective irrigation value
    float water_demand;  // the amount of water that the section will need
    long relay_num;      // the relay number of that garden section
    long controller_num; // the ESP module's number for that section of the garden
} garden_section;


struct write_result {
    char *data;
    int pos;
};


static size_t write_response(void *ptr, size_t size, size_t nmemb, void *stream) {
    /* Save the GET response from into write_result struct, function from jannson example code */
    struct write_result *result = (struct write_result *)stream;

    if (result->pos + size * nmemb >= BUFFER_SIZE - 1) {
        fprintf(stderr, "error: too small buffer\n");
        return 0;
    }

    memcpy(result->data + result->pos, ptr, size * nmemb);
    result->pos += size * nmemb;

    return size * nmemb;
}


static int newline_offset(const char *text) {
    /* Return the offset of the first newline in text or the length of
   text if there's no newline */
    const char *newline = strchr(text, '\n');
    if(!newline)
        return strlen(text);
    else
        return (int)(newline - text);
}


static char *request(const char *url) {
    /* CURL GET request from Jansson's github_commit.c example*/
    CURL *curl = NULL;
    CURLcode status;
    struct curl_slist *headers = NULL;
    char *data = NULL;
    long code;

    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    if (!curl)
        goto error;

    data = malloc(BUFFER_SIZE);
    if (!data)
        goto error;

    struct write_result write_result = {.data = data, .pos = 0};

    curl_easy_setopt(curl, CURLOPT_URL, url);

    /* GitHub commits API v3 requires a User-Agent header */
    // headers = curl_slist_append(headers, "User-Agent: Jansson-Tutorial");
    // curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_response);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &write_result);

    status = curl_easy_perform(curl);
    if (status != 0) {
        fprintf(stderr, "error: unable to request data from %s:\n", url);
        fprintf(stderr, "%s\n", curl_easy_strerror(status));
        goto error;
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    if (code != 200) {
        fprintf(stderr, "error: server responded with code %ld\n", code);
        goto error;
    }

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    curl_global_cleanup();

    /* zero-terminate the result */
    data[write_result.pos] = '\0';

    return data;

error:
    if (data)
        free(data);
    if (curl)
        curl_easy_cleanup(curl);
    if (headers)
        curl_slist_free_all(headers);
    curl_global_cleanup();
    return NULL;
}


cimis_results parse_cimis_json(json_t *json_root){
    // to check what type it actually is, go to https://jansson.readthedocs.io/en/2.8/apiref.html#c.json_type
    // and check typeof (it's an int and the types are listed in order)

    // obtain the Eto values for each day requested to CIMIS
    json_t *Data, *Providers, *get_records, *Records;
    const char *eto_value, *precip_value; // values adding up the total precipitation and ETo over specified range
    cimis_results cimis_out;

    int error_count = 0;
    float total_precipitation = 0.0; 
    float total_eto = 0.0;

    Data = json_object_get(json_root, "Data");
    // Data has to be an object
    if (!json_is_object(Data)) {
        // fprintf(stderr, "error: Data is not an object\n");
        // printf("    Data is a(n) %d\n", json_typeof(json_root));
        error_count++;
    } 
    Providers = json_object_get(Data, "Providers");
    if (!json_is_array(Providers)) {
        // fprintf(stderr, "error: Providers is not an array\n");
        // printf("    is Providers is an array? (1==yes) %d\n", json_is_array(Providers));
        error_count++; 
    } 
    get_records = json_array_get(Providers, 0);
    if (!json_is_object(get_records)) {
        error_count++;  
    } 
    Records = json_object_get(get_records, "Records");
    if (!json_is_array(Records)) {
        error_count++; 
    }
    // else {
    //     printf("The number of days being analyzed is %lu days\n", json_array_size(Records));
    // }

    for (int i = 0; i < json_array_size(Records); i++) {
        json_t *get_daydata, *DayAsceEto, *EToValue, *DayPrecip, *PrecipValue;

        get_daydata = json_array_get(Records, i);
        if (!json_is_object(get_daydata)) {
            error_count++;  
        } 
        DayAsceEto = json_object_get(get_daydata, "DayAsceEto");
        if (!json_is_object(DayAsceEto)) {
            error_count++; 
        } 
        EToValue = json_object_get(DayAsceEto, "Value");
        if (!json_is_string(EToValue)) {
            error_count++; 
        } 

        eto_value = json_string_value(EToValue);
            // printf("%.8s %.*s\n", json_string_value(Providers), newline_offset(eto_value),
            //     eto_value);
        total_eto = total_eto + strtof(eto_value, NULL);

        // *************** get the Precipitation data for the day *************** //
        DayPrecip = json_object_get(get_daydata, "DayPrecip");
        if (!json_is_object(DayPrecip)) {
            error_count++; 
        }

        PrecipValue = json_object_get(DayPrecip, "Value");
        if (json_is_null(PrecipValue)) { 
            // precipitation could be null, just set as zero for now (likely that they don't have the data for the whole day so they provide null)
            // precip_value = json_string_value(PrecipValue);
            printf("Precipitation value for the day is null (probably the last day if only one message appears!)\n");
        } else if (!json_is_string(PrecipValue)) {
            // fprintf(stderr, "error: PrecipValue is not a string\n");
            // printf("    PrecipValue is a(n) %d\n", json_typeof(PrecipValue));
            error_count++; 
        } else {
            precip_value = json_string_value(PrecipValue);
            // printf("%.8s %.*s\n", json_string_value(Providers), newline_offset(precip_value),
            // precip_value);
            total_precipitation = total_precipitation + strtof(precip_value, NULL);
        }
   
    }

    cimis_out.parse_errors = error_count;
    cimis_out.Et0 = total_eto;
    cimis_out.precip = total_precipitation;

    return cimis_out;
}


const char * get_json_string(char *Val, json_t *json_data){
    json_t *getVal;
    const char *value;

    getVal = json_object_get(json_data, Val);
    if (!json_is_string(getVal)) {
        printf("Is this not a String?\n");
        printf("    getVal is a(n) %d\n", json_typeof(getVal));
    }

    return json_string_value(getVal);
}


long get_json_long(char *Val, json_t *json_data){
    json_t *getVal;
    long value;

    getVal = json_object_get(json_data, Val); // obtain the int val from JSON data
    value = json_integer_value(getVal);       // convert to an int/long
    if (value == 0) {
        printf("Either the return is zero (offline controllers or relays) or json is not an int?\n");
        // printf("    getVal is a(n) %d\n", json_typeof(getVal));
    }

    return value;
}


double get_json_double(char *Val, json_t *json_data){
    json_t *getVal;
    double value;

    getVal = json_object_get(json_data, Val); // obtain the int val from JSON data
    value = json_real_value(getVal);       // convert to an int/long
    if (value == 0.) {
        printf("Either the return is zero or json is not an int\n");
        // printf("    getVal is a(n) %d\n", json_typeof(getVal));
    }

    return value;
}


void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg) {

    char * curr_payload = (char *) msg->payload;

    curr_contrlr_done = atoi(curr_payload);
    curr_relay_done = atoi(curr_payload+1);

	printf("New message with topic %s: %s\n", msg->topic, (char *) msg->payload);
}


int main(){
    char *cimis_station = CIMIS_STATION;
    char *cimis_app_key = APP_KEY;
    // printf("Example of using CMake input file, \n     Irrigation Major Version %d \n     Irrigation Minor Version %d \n", Irrigation_VERSION_MAJOR, Irrigation_VERSION_MINOR);

    time_t date_today, date_start;
    char today_buffer[80], start_buffer[80];

    struct tm tm_out_today, tm_out_start;

    int num_days = 7; // how many days of data do we want to use?

    size_t i;
    char *text;

    // CIMIS JSON file
    json_t *root;
    json_error_t error;
    // Last irrigation JSON file
    json_t *root_irr;
    json_error_t error_irr;

    // provides the current date and time in seconds since the Epoch for the end date provided to CIMIS
    time(&date_today);
    date_today = date_today - 86400; // use previous day's data since the current date's data will all be NULL
    // subtract the number of days (in seconds) of desired CIMIS data to obtain a start date
    date_start = date_today - num_days*86400;

    // represent the start and end dates in date and time components (local time)
    localtime_r(&date_today, &tm_out_today);
    localtime_r(&date_start, &tm_out_start);

    // format the strings according to the format specified by CIMIS 
    strftime(today_buffer, sizeof(today_buffer), "%Y-%m-%d", &tm_out_today);
    strftime(start_buffer, sizeof(start_buffer), "%Y-%m-%d", &tm_out_start);

    // stitch together the file name strings
    char *file_name = malloc(strlen("cimis_") + strlen(start_buffer) + strlen("_") + strlen(today_buffer) + strlen(".json") + 1); // +1 for the null-terminator
    // make function that has full_url passed as a pointer ref to concatenate the values internally, but can still free the pointer outside of the function
    // add checks for errors in malloc here
    strcpy(file_name, "cimis_");
    strcat(file_name, start_buffer);
    strcat(file_name, "_");
    strcat(file_name, today_buffer);
    strcat(file_name, ".json");

    // char *file_name = "cimis_2023-07-06_2023-07-13.json";   // hard coded for testing, see above code for real file name creation, will cause an invalide free at the end 
    // printf("Testing to see if file %s exists\n", file_name);

    // check if call has already been made for this dataset by checking if a file already exists for it
    FILE *open_file = fopen(file_name, "r");
    if (open_file == NULL) {
        printf("File for desired dates does not exist, requesting data from CIMIS");

        // set up the url link with the API key, weather station number, and start and end dates for the data (obtain daily weather data)
        char *full_url = malloc(strlen("https://et.water.ca.gov/api/data?appKey=") + strlen(cimis_app_key) + strlen("&targets=") + strlen(cimis_station) + strlen("&startDate=") + strlen(start_buffer) + strlen("&endDate=") + strlen(today_buffer) + 1); 
        // make function that has full_url passed as a pointer ref to concatenate the values internally, but can still free the pointer outside of the function
        // add checks for errors in malloc here
        strcpy(full_url, "https://et.water.ca.gov/api/data?appKey=");
        strcat(full_url, cimis_app_key);
        strcat(full_url, "&targets=");
        strcat(full_url, cimis_station);
        strcat(full_url, "&startDate=");
        strcat(full_url, start_buffer);
        strcat(full_url, "&endDate=");
        strcat(full_url, today_buffer);

        // printf("\nGET call to url: \n%s\n", full_url);

        // Jansson's function call to request data from url from Jannson's github_commit.c example
        text = request(full_url);
        if(!text)
            return 1;

        root = json_loads(text, 0, &error);
        // printf("%s", text);
        free(text);

        if (!root) {
            fprintf(stderr, "error: on line %d: %s\n", error.line, error.text);
            return 1;
        }

        // dump json into a file
        json_dump_file(root, file_name, 0);
        printf("JSON being saved to file: %s\n", file_name);

        free(full_url); // deallocate the file_name string
        printf("CIMIS data obtained and cached\n");

    }
    else {
        // The file exists which means a GET call has already been made for this date range
        printf("File for desired dates already exists, opening JSON file %s\n", file_name);
        root = json_load_file(file_name, 0, &error);
        if(!root) {
            printf("Could not open file %s\n", file_name);
            fprintf(stderr, "ERROR: on line %d: %s\n", error.line, error.text);
            return 1;
        }
        // clean up by closing the file
        fclose(open_file);
    }

    cimis_results cimis_out;
    cimis_out = parse_cimis_json(root);
    if (cimis_out.parse_errors > 0){
        fprintf(stderr, "ERROR: there were %d type errors when parsing the Et0 JSON file.\n", cimis_out.parse_errors);
        return 1;
    }
    json_decref(root);

    printf("CIMIS Et0 reads %.2f\n", cimis_out.Et0);
    printf("CIMIS precip reads %.2f\n", cimis_out.precip);

        
    char *irrigation_file = "irrigation_example.json";
    FILE *open_last_file = fopen(irrigation_file, "r");

    printf("opening JSON irrigation file, irrigation_example.json\n");
    root_irr = json_load_file(irrigation_file, 0, &error_irr);
    if(!root_irr) {
        printf("Could not open irrigation file %s\n", file_name);
        fprintf(stderr, "ERROR: on line %d: %s\n", error_irr.line, error_irr.text);
        return 1;
    }

    json_t *Data, *get_records;
    const char *irr_value; // values adding up the total precipitation and ETo over specified range

    // 
    Data = json_object_get(root_irr, "Data");
    // Data is an array, but we first get it as an object
    if (!json_is_array(Data)) {
        fprintf(stderr, "error: Data is not an array\n");
        printf("    Data is a(n) %d\n", json_typeof(root_irr));
        // error_count++;
    } 
    
    // calculate the effective precipitation from CIMIS data
    float effective_precipitation = cimis_out.precip * 0.5 * 0.623; // in gallons
    // need to iterate through data over all the garden sections
    long int num_sections = json_array_size(Data);
    printf("The number of garden sections with separate irrigation systems is: %ld\n\n", num_sections);
    printf("---------------------------------------------------------------------\n");
    printf("   Section    |         Gallons of H2O needed to meet demand\n");
    printf("---------------------------------------------------------------------\n");

    garden_section section_array[num_sections];

    for (int i = 0; i < num_sections; i++) {
        float amount_irrigated = 0.;
        float effective_irrigation = 0.;
        const char *date_str;
        char today_irr_buffer[80], demand_ceil[50];
        struct tm last_tm_irrigated;
        time_t t_irr = time(NULL);

        section_array[i].water_demand = 0.;

        get_records = json_array_get(Data, i);
        if (!json_is_object(get_records)) {
            printf("error getting the objects within the array at loop %d\n", i); 
        } 

        section_array[i].name = get_json_string("Name", get_records);
        section_array[i].relay_num = get_json_long("Relay", get_records);
        section_array[i].controller_num = get_json_long("Controller", get_records);

        // printf("The name of the section is %s\n", section_array[i].name);
        section_array[i].PF = strtof(get_json_string("PF", get_records), NULL);
        section_array[i].LA = get_json_long("LA", get_records);
        // printf("the LA is %ld\n", section_array[i].LA);

        // obtain the date when irrigation happened last and find the difference between it and the current date
        date_str = get_json_string("Date", get_records);
        if (strptime(date_str, "%Y-%m-%d %T", &last_tm_irrigated) == NULL) {
            fprintf(stderr, "error: unable to convert string to tm struct\n");
            return -1;
        } 

        last_tm_irrigated.tm_isdst = -1;   // avoid manually determining if DST or not
        // puts( asctime(&last_tm_irrigated) );

        section_array[i].days_since = difftime(date_today, mktime(&last_tm_irrigated)) / 86400;  // converts from seconds to days
        // printf("%.f days have passed since last irrigation.\n", section_array[i].days_since);

        if (section_array[i].days_since > 7.) {
            // don't include irrigation in current calculations
            section_array[i].days_since = 0.;
        } else {
            // amount_irrigated = get_json_long("Gallons", get_records);
            amount_irrigated = strtof(get_json_string("Gallons", get_records), NULL);
            effective_irrigation = amount_irrigated * 0.7; // in gallons, drip irrigation is not 100% effective, but better than flood
            // effective_irrigation = (float)amount_irrigated * 0.7; // in gallons, drip irrigation is not 100% effective, but better than flood
        }

        section_array[i].water_demand = (cimis_out.Et0 * section_array[i].PF * section_array[i].LA * 0.623) - effective_precipitation - effective_irrigation;  // in gallons
        if (section_array[i].water_demand <= 0.) {
            // zero or negative water demand mean that there is no need for irrigation 
            section_array[i].water_demand = 0.;
        } else {
            printf("   %s", section_array[i].name);
            printf("   |                       %.3f   \n", section_array[i].water_demand);
            printf("---------------------------------------------------------------------\n");

            // setup irrigation json updating only for online relays
            if (section_array[i].relay_num > 0 && section_array[i].controller_num > 0) {
                // save the new date to "Date" and amount watered to "Gallons" to update the irrigation json later (after confirming that it was watered)
                strftime(today_irr_buffer, sizeof(today_irr_buffer), "%Y-%m-%d %T", &tm_out_today);
                // printf("The new date is %s\n", today_irr_buffer);
                json_object_set(get_records, "Date", json_string(today_irr_buffer));

                snprintf(demand_ceil, 50, "%f", section_array[i].water_demand); 
                json_object_set(get_records, "Gallons", json_string(demand_ceil));
            }
        }
    }

    // setup for MQTT messages to communicate with ESP controlling relay modules
    struct mosquitto *mosq;     /**< Libmosquito MQTT client instance. */

    int mosq_error = 0;

    //libmosquitto initialization
    mosquitto_lib_init();

    //Create new libmosquitto client instance
    mosq = mosquitto_new("irrig_calculator", true, NULL);
    mosquitto_message_callback_set(mosq, on_message);

    if (!mosq) {
	    printf("Error: failed to create mosquitto client\n");
        mosq_error += 1;
    }

    // connect with a password and user ID
    if (mosquitto_username_pw_set(mosq, mqtt_ssid, mqtt_password) != MOSQ_ERR_SUCCESS) {
        printf("Error: failed to connect using the provided user ID and password\n");
        mosq_error += 1;
    }

    //Connect to MQTT broker
    if (mosquitto_connect(mosq, host_id, 1883, 60) != MOSQ_ERR_SUCCESS) {
	    printf("Error: connecting to MQTT broker failed\n");
        mosq_error += 1;
    }

    if (mosq_error > 1) {
        printf("Error: was unable to connect to MQTT broker, stopping program");

        // clean up by closing the file and json root for irrigation file
        fclose(open_last_file);
        json_decref(root_irr);
        // deallocate the CIMIS file_name string
        free(file_name); 
        //Clean up/destroy objects created by libmosquitto
        mosquitto_destroy(mosq);
        mosquitto_lib_cleanup();

        return -1;
    }

    // subscribe to the ESP's callback
    mosquitto_subscribe(mosq, NULL, "/relay_done", 0);

    printf("Now connected to the broker!\n");
    // Call to start a new thread to process network traffic
    mosquitto_loop_start(mosq);

    for (int i = 0; i < num_sections; i++) {
        // send messages to ESP to turn on relays for a given amount of time
        if (section_array[i].water_demand > 0. && (section_array[i].relay_num > 0 && section_array[i].controller_num > 0)) { 
            // make sure there are offline controllers and relays are set at 0 so that those can be ignored until they come online

            // drip irigation units are gal/hr and we need to send msec to ESP
            long irr_timer = (long)(1000 * section_array[i].water_demand); // assumes drip is set to 1 gal/sec for testing -> keeps the times shorter
            // long irr_timer = (long)(3600*1000 * section_array[i].water_demand); 
            printf("Section %s will be watered for %lu\n", section_array[i].name, irr_timer);
            printf("turning ON relay %lu\n", section_array[i].relay_num);

            char mssg_out[7];
            sprintf(mssg_out, "%lu %lu", section_array[i].relay_num, irr_timer);

            if (section_array[i].controller_num == 1) {
                mosquitto_publish(mosq, NULL, "/back_yard", 7, mssg_out, 0, false);                
            } // add more topics as the ESPs come online

            int sleep_time = (int)(irr_timer)/1000 + 1; // add two seconds so that the relays never turn on at the same time (pressure problems if using the same source of water)
            printf("computer sleeps for %d seconds\n", sleep_time);

            sleep(sleep_time); // sleep for the expected amount of time the relay is on plus another second (add a return message later that stops sleep?)

            // check to see if there was a return message from the correct controller and relay
            if (curr_contrlr_done == section_array[i].controller_num && curr_relay_done == section_array[i].relay_num) {
                printf("Garden section successfully watered!\n\n");
            }
        }

    }

    mosquitto_loop_stop(mosq, true);

    // create updated irrigation json file
    if (json_dump_file(root_irr, "./irrigation_example.json", 0)) {
        fprintf(stderr, "cannot save json to file\n");
    }

    // clean up by closing the file and json root for irrigation file
    fclose(open_last_file);
    json_decref(root_irr);
    // deallocate the CIMIS file_name string
    free(file_name); 
    //Clean up/destroy objects created by libmosquitto
    mosquitto_disconnect(mosq);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    
    return(0);
}
