#include "mbed.h"
#include "mbed_rpc.h"
#include <string.h>
#include <vector>
#include <math.h>

#include "MQTTNetwork.h"
#include "MQTTmbed.h"
#include "MQTTClient.h"
#include "stm32l475e_iot01_accelero.h"

#include "uLCD_4DGL.h"

#include "accelerometer_handler.h"
#include "config.h"
#include "magic_wand_model_data.h"
 
#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/micro/kernels/micro_ops.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/version.h"

#include "mbed_events.h"
using namespace std::chrono;
//headers import endline ******************************start********************************************


//variable declaration 

//GLOBAL variables related to wifi module
WiFiInterface *wifi;
volatile int message_num = 0;
volatile int arrivedcount = 0;
volatile bool closed = false;

const char* topic = "Mbed";
Thread mqtt_thread(osPriorityHigh);
EventQueue mqtt_queue;

int16_t pDataXYZ[3] = {0}; //accelaration data declaration
InterruptIn btn(USER_BUTTON);
EventQueue queue(32 * EVENTS_EVENT_SIZE);
Thread t;

DigitalOut led1(LED1);
DigitalOut led2(LED2);
DigitalOut led3(LED3);

//rpc control 
RpcDigitalOut myled1(LED1,"myled1");
RpcDigitalOut myled2(LED2,"myled2");
RpcDigitalOut myled3(LED3,"myled3");
BufferedSerial pc(USBTX, USBRX);
void rpcstage01(Arguments *in, Reply *out);
RPCFunction rpcges(&GestureCapture, "rpcstage01"); //screen type => /rpcstage01/run 1
void rpcstage02(Arguments *in, Reply *out)
RPCFunction rpcfex(&FeatureExtract, "rpcstage02"); //RPCFunction name cannot be the same



//function declaration
//tool function
void GestureCapture(){
    led1 = led2 = 1; //start
    printf("test queue\r\n");
    demoGes = -1; //init
    uLCD.background_color(0x000000); //white
    uLCD.cls();

    while(mode){
      
      if (!btn){
        if (demoGes == 0 || demoGes == 1){
          selectGes = demoGes;
          publish = true;
          break;
        }
      }

      // Attempt to read new data from the accelerometer
      got_data = ReadAccelerometer(error_reporter, model_input->data.f,
                                  input_length, should_clear_buffer);

      // If there was no new data,
      // don't try to clear the buffer again and wait until next time
      if (!got_data) {
        should_clear_buffer = false;
        continue;
      }

      // Run inference, and report any error
      TfLiteStatus invoke_status = interpreter->Invoke();
      if (invoke_status != kTfLiteOk) {
        error_reporter->Report("Invoke failed on index: %d\n", begin_index);
        continue;
      }

      // Analyze the results to obtain a prediction
      gesture_index = PredictGesture(interpreter->output(0)->data.f);
      if (gesture_index != 2){
          demoGes = gesture_index;
      }

      // Clear the buffer next time we read data
      should_clear_buffer = gesture_index < label_num;

      // Produce an output
      if (gesture_index < label_num) {
        error_reporter->Report(config.output_message[gesture_index]);
      }
    }

    led1 = led2 = 0; //end
    
    mqtt_thread.start(callback(&mqtt_queue, &EventQueue::dispatch_forever));
    printf("end queue\r\n");


}


int FeatureExtract(int gesacc){

    length = sizeof(ges)/sizeof(ges[0]);

    x_a = gesacc[0];
    y_a = gesacc[1];
    z_a = gesacc[2];
    



    
    return;

}



int PredictGesture(float* output) {
     // How many times the most recent gesture has been matched in a row
     static int continuous_count = 0;
     // The result of the last prediction
     static int last_predict = -1;
 
     // Find whichever output has a probability > 0.8 (they sum to 1)
     int this_predict = -1;
     for (int i = 0; i < label_num; i++) {
        if (output[i] > 0.8) this_predict = i;
     }
 
     // No gesture was detected above the threshold
     if (this_predict == -1) {
        continuous_count = 0;
        last_predict = label_num;
        return label_num;
     }
 
     if (last_predict == this_predict) {
        continuous_count += 1;
      } else {
        continuous_count = 0;
      }
      last_predict = this_predict;
 
      // If we haven't yet had enough consecutive matches for this gesture,
      // report a negative result
     if (continuous_count < config.consecutiveInferenceThresholds[this_predict]) {
        return label_num;
      }
      // Otherwise, we've seen a positive result, so clear all our variables
      // and report it
      continuous_count = 0;
      last_predict = -1;
 
       return this_predict;
 }



//rpc control function
void rpccon(){
    //The mbed RPC classes are now wrapped to create an RPC enabled version - see RpcClasses.h so don't add to base class

    // receive commands, and send back the responses
    char buf[256], outbuf[256];

    FILE *devin = fdopen(&pc, "r");
    FILE *devout = fdopen(&pc, "w");

    while(1) {
        memset(buf, 0, 256);
        for (int i = 0; ; i++) {
            char recv = fgetc(devin);
            if (recv == '\n') {
                printf("\r\n");
                break;
            }
            buf[i] = fputc(recv, devout);
        }
        //Call the static call method on the RPC class
        RPC::call(buf, outbuf);
        printf("%s\r\n", outbuf);
    }
}

//objects
DigitalOut led1(LED1);
DigitalOut led2(LED2);
DigitalOut led3(LED3);
DigitalIn btn(USER_BUTTON);
EventQueue eventQueue;
// normal priority thread for other events
Thread eventThread(osPriorityLow);
int mode = 0;
int selectGes = -1;
int demoGes = -1;






//tf. declaring stage************************************************************************************

bool should_clear_buffer = false;
bool got_data = false;
 
// The gesture index of the prediction
int gesture_index;

// Set up logging.
static tflite::MicroErrorReporter micro_error_reporter;
tflite::ErrorReporter* error_reporter = &micro_error_reporter;

// Create an area of memory to use for input, output, and intermediate arrays.
// The size of this will depend on the model you're using, and may need to be
// determined by experimentation.
constexpr int kTensorArenaSize = 60 * 1024;
uint8_t tensor_arena[kTensorArenaSize];
// Return the result of the last prediction
int PredictGesture(float* output);
TfLiteTensor* model_input;
  // Map the model into a usable data structure. This doesn't involve any
  // copying or parsing, it's a very lightweight operation.
  const tflite::Model* model = tflite::GetModel(g_magic_wand_model_data);

  static tflite::MicroOpResolver<6> micro_op_resolver;
// Build an interpreter to run the model with
  static tflite::MicroInterpreter static_interpreter(

      model, micro_op_resolver, tensor_arena, kTensorArenaSize, error_reporter);

  tflite::MicroInterpreter* interpreter = &static_interpreter;

//MQTT function stage*******************************************
void messageArrived(MQTT::MessageData& md) {
      MQTT::Message &message = md.message;
      char msg[300];
      sprintf(msg, "Message arrived: QoS%d, retained %d, dup %d, packetID %d\r\n", message.qos, message.retained, message.dup, message.id);
      printf(msg);
      ThisThread::sleep_for(1000ms);
      char payload[300];
      sprintf(payload, "Payload %.*s\r\n", message.payloadlen, (char*)message.payload);
      printf(payload);
      ++arrivedcount;
  }

void publish_message(MQTT::Client<MQTTNetwork, Countdown>* client) {
      message_num++;
      MQTT::Message message;
      char buff[100];
      BSP_ACCELERO_AccGetXYZ(pDataXYZ);
      sprintf(buff, "Qos0 %d, %d, %d", pDataXYZ[0], pDataXYZ[1], pDataXYZ[2]);
      message.qos = MQTT::QOS0;
      message.retained = false;
      message.dup = false;
      message.payload = (void*) buff;
      message.payloadlen = strlen(buff) + 1;
      int rc = client->publish(topic, message);

}

void close_mqtt() {
      closed = true;
}



//main function**************************************************

int main(){

  BSP_ACCELERO_Init();

  if (model->version() != TFLITE_SCHEMA_VERSION) {

    error_reporter->Report(
      "Model provided is schema version %d not equal to supported version %d.",
      model->version(), TFLITE_SCHEMA_VERSION);
    return -1;

  }


  // Pull in only the operation implementations we need.
  // This relies on a complete list of all the ops needed by this graph.
  // An easier approach is to just use the AllOpsResolver, but this will
  // incur some penalty in code space for op implementations that are not
  // needed by this graph.
  
  micro_op_resolver.AddBuiltin(
    tflite::BuiltinOperator_DEPTHWISE_CONV_2D,
    tflite::ops::micro::Register_DEPTHWISE_CONV_2D());

  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_MAX_POOL_2D,
                               tflite::ops::micro::Register_MAX_POOL_2D());

  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_CONV_2D,
                               tflite::ops::micro::Register_CONV_2D());

  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_FULLY_CONNECTED,
                               tflite::ops::micro::Register_FULLY_CONNECTED());

  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_SOFTMAX,
                               tflite::ops::micro::Register_SOFTMAX());

  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_RESHAPE,
                               tflite::ops::micro::Register_RESHAPE(), 1);

  // Allocate memory from the tensor_arena for the model's tensors
  interpreter->AllocateTensors();
  // Obtain pointer to the model's input tensor
  model_input = interpreter->input(0);

  if ((model_input->dims->size != 4) || (model_input->dims->data[0] != 1) ||
      (model_input->dims->data[1] != config.seq_length) ||
      (model_input->dims->data[2] != kChannelNumber) ||
      (model_input->type != kTfLiteFloat32)) {
      error_reporter->Report("Bad input tensor parameters in model");
      return -1;
  }


  input_length = model_input->bytes / sizeof(float);

  TfLiteStatus setup_status = SetupAccelerometer(error_reporter);
  if (setup_status != kTfLiteOk) {
    error_reporter->Report("Set up failed\n");
    return -1;
  }


  error_reporter->Report("Set up successful...\n");
  led1 = led2 = led3 = 0;//init



  wifi = WiFiInterface::get_default_instance();
  if (!wifi) {
    printf("ERROR: No WiFiInterface found.\r\n");
    return -1;
  }
  
  printf("\nConnecting to %s...\r\n", MBED_CONF_APP_WIFI_SSID);
  int ret = wifi->connect(MBED_CONF_APP_WIFI_SSID, MBED_CONF_APP_WIFI_PASSWORD, NSAPI_SECURITY_WPA_WPA2);
  if (ret != 0) {
    printf("\nConection error: %d\r\n", ret);
    return -1;
  }

  NetworkInterface* net = wifi;
  MQTTNetwork mqttNetwork(net);
  MQTT::Client<MQTTNetwork, Countdown> client(mqttNetwork);

  //TODO: revise host to your IP
  const char* host = "172.20.10.2";

  printf("Connecting to TCP network...\r\n");
  SocketAddress sockAddr;

  sockAddr.set_ip_address(host);
  sockAddr.set_port(1883);
  printf("address is %s/%d\r\n", (sockAddr.get_ip_address() ? sockAddr.get_ip_address() : "None"),  (sockAddr.get_port() ? sockAddr.get_port() : 0) ); //check setting
  int rc = mqttNetwork.connect(sockAddr);//(host, 1883);

  if (rc != 0) {
    printf("Connection error.");
    return -1;
  }

  printf("Successfully connected!\r\n");

  

  MQTTPacket_connectData data = MQTTPacket_connectData_initializer;

  data.MQTTVersion = 3;
  data.clientID.cstring = "Mbed";
  if ((rc = client.connect(data)) != 0){
    printf("Fail to connect MQTT\r\n");

  }

  if (client.subscribe(topic, MQTT::QOS0, messageArrived) != 0){
    printf("Fail to subscribe\r\n");

  }

  /*start program*/
  eventQueue.call(rpccon);
  //mqtt_thread.join(); //start(callback(&mqtt_queue, &EventQueue::break_dispatch));
  eventThread.start(callback(&eventQueue, &EventQueue::dispatch_forever));

//   mqtt_thread.start(callback(&mqtt_queue, &EventQueue::dispatch_forever));
  mqtt_queue.call_every(2s, publish_message, &client);
   
//btn2.rise(mqtt_queue.event(&publish_message, &client));
//btn3.rise(&close_mqtt);


  int num = 0;
  while (num != 5) {
    client.yield(100);    
    ++num;
  }

  while (1) {
    
    printf("while true in main\n");
    if (closed) break;
    client.yield(500);
    //ThisThread::sleep_for(500ms);
  }

  printf("Ready to close MQTT Network......\n");

  if ((rc = client.unsubscribe(topic)) != 0) {
    printf("Failed: rc from unsubscribe was %d\n", rc);
  }

  if ((rc = client.disconnect()) != 0) {
    printf("Failed: rc from disconnect was %d\n", rc);
  }


  mqttNetwork.disconnect();
  printf("Successfully closed!\n");

  return 0;
}





//RPC function declaration******************************************
void rpcstage01(Arguments *in, Reply *out){  

    mode = in->getArg<double>();
    eventQueue.call(GestureCapture);
    //mqtt_thread.join(); //start(callback(&mqtt_queue, &EventQueue::break_dispatch));
    eventThread.start(callback(&eventQueue, &EventQueue::dispatch_forever));

}

void rpcstage02(Arguments *in, Reply *out){

    mode = in->getArg<double>();
    eventQueue.call(FeatureExtract);
    //mqtt_thread.join(); //start(callback(&mqtt_queue, &EventQueue::break_dispatch));
    eventThread.start(callback(&eventQueue, &EventQueue::dispatch_forever));

}