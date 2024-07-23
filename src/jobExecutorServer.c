#include <stdio.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <signal.h>






void perror_exit(char *message){
    perror(message);
    exit(EXIT_FAILURE);
}
//Συνάρτηση υπολογισμού των ψηφίων ενός αριθμού 
int countDigits(int number) {
    int count = 0;
    
    // Αν ο αριθμός είναι αρνητικός, τον κάνουμε θετικό
    if (number < 0) {
        number = -number;
    }
    
    // Αν ο αριθμός είναι 0, τότε έχει ακριβώς 1 ψηφίο
    if (number == 0) {
        return 1;
    }
    
    // Υπολογισμός του αριθμού των ψηφίων
    while (number != 0) {
        number /= 10;
        count++;
    }
    
    return count;
}

//Struct για το περιεχόμενο κάθε στοιχείου του ενταμιευτή 
typedef struct{
    char *job_id;
    char * job;
    int clientSocket;
    int job_is_removed;
    int  pid;
}buffer_value;
//Struct για το περιεχόμενο του buffer
typedef struct {
    buffer_value *data;
    int start;
    int end;
    int count;
    int jobs_removed;
    int size;
    pthread_mutex_t buffer_mutex;
    pthread_cond_t cond_nonempty_or_concurrency;
    pthread_cond_t cond_nonfull;
} Buffer;
// Struct που περιέχει τα τρία ορίσματα που θέλουμε να περάσουμε στα controled threads
typedef struct {
    int clientSocket;
    int threadPoolSize;
    int serverSocket;
} ThreadArgs;
//Καθολικές μεταβλητές 
int concurrency = 1;
int jobs_running = 0;
Buffer buffer;
pthread_mutex_t concurrency_mutex;
pthread_mutex_t processes_mutex;
pthread_cond_t terminate;
int server_terminate = 0;
//Συνάρτηση αρχικοποίησης του buffer
void initialize(int size) {
    buffer.data = malloc(sizeof(buffer_value)*size);
    buffer.start = 0;
    buffer.end = -1;
    buffer.count = 0;
    buffer.jobs_removed = 0;
    buffer.size = size;
    pthread_mutex_init(&buffer.buffer_mutex,0);
    pthread_cond_init(&buffer.cond_nonempty_or_concurrency,0);
    pthread_cond_init(&buffer.cond_nonfull,0); 
   
}
//Συνάρτηση καταστροφής του buffer
void destroy_buffer(Buffer buffer){
    pthread_cond_destroy(&buffer.cond_nonempty_or_concurrency);
    pthread_cond_destroy(&buffer.cond_nonfull);
    pthread_mutex_destroy(&buffer.buffer_mutex);
    free(buffer.data);
}
//Συνάρτηση προσθήκης στοιχείου στον buffer
void place(buffer_value data) {
    pthread_mutex_lock(&buffer.buffer_mutex);
    while (buffer.count >= buffer.size) {
        printf(">> Found Buffer Full \n");
        pthread_cond_wait(&buffer.cond_nonfull, &buffer.buffer_mutex);
    }
    buffer.end = (buffer.end + 1) % buffer.size;
    buffer.data[buffer.end] = data;
    buffer.count++; 
    pthread_cond_signal(&buffer.cond_nonempty_or_concurrency);
    pthread_mutex_unlock(&buffer.buffer_mutex);
}
//Συνάρτηση αφέραισης στοιχείου από την αρχή του buffer
buffer_value obtain() {
    buffer_value data;
    pthread_mutex_lock(&buffer.buffer_mutex);
    while (buffer.count <= 0 || concurrency <= jobs_running){
        printf(">> Found Buffer Empty \n");
        if(server_terminate == 1)
            return data;
        pthread_cond_wait(&buffer.cond_nonempty_or_concurrency, &buffer.buffer_mutex);
        
	    if(server_terminate == 1)
	        return data;
        
       
    }
    data = buffer.data[buffer.start];
    pthread_mutex_lock(&processes_mutex);
    //Ελέγχουμε αν η job έχει γίνει ήδη remove και μειώσαμε το count στην συνάρτηση remove οπότε δεν χρειάζεται να το ξανακάνουμε
    if(buffer.data[buffer.start].job_is_removed == 0 && server_terminate == 0){
        buffer.count--;
        jobs_running++;//Αν η job έχει διαγραφεί δεν θα τρέξει άρα δεν χρειάζεται α αυξηθεί
    }
    pthread_mutex_unlock(&processes_mutex);
        
    buffer.start = (buffer.start + 1) % buffer.size;
    pthread_cond_signal(&buffer.cond_nonfull);
    pthread_mutex_unlock(&buffer.buffer_mutex);
    return data;
}
//Συνάρτηση διαγραφής στοιχείου στην θέση potision του buffer
void remove_job(int potision) {
    pthread_mutex_lock(&buffer.buffer_mutex);
    free(buffer.data[potision].job_id);
    free(buffer.data[potision].job);
    buffer.data[potision].job_is_removed= 1;
    if(write(buffer.data[potision].clientSocket,&buffer.data[potision].job_is_removed,sizeof(int)) <= 0){
        close(buffer.data[potision].clientSocket);
        perror_exit("write data.job_is_removed");
    }
    buffer.count--;
    buffer.jobs_removed++;
    pthread_cond_signal(&buffer.cond_nonfull);
    pthread_mutex_unlock(&buffer.buffer_mutex);
    
}
//Συνάρτηση διαγραφής στοιχείου από τον buffer επειδή ο server τερματίζει
void remove_job_because_server_terminates(int potision) {
    pthread_mutex_lock(&buffer.buffer_mutex);
    free(buffer.data[potision].job_id);
    free(buffer.data[potision].job);
    if(write(buffer.data[potision].clientSocket,&buffer.data[potision].job_is_removed,sizeof(int)) <= 0){
        close(buffer.data[potision].clientSocket);
        perror_exit("write data.job_is_removed");
    }
    buffer.data[potision].job_is_removed= 1;
    char message[100]; 
    strcpy(message,"SERVER TERMINATED BEFORE EXECUTION");
    int size_of_message = 100 ;
    //Γράφουμε το μέγεθος του μηνύματος στον commander
    if(write(buffer.data[potision].clientSocket,&size_of_message,sizeof(int)) <= 0){
        close(buffer.data[potision].clientSocket);
        perror_exit("write size of message SERVER TERMINATED  BEFORE EXECUTION");
    }
    //Γράφουμε το μήνυμα στον commander
    if(write(buffer.data[potision].clientSocket,message,size_of_message) <= 0){
        close(buffer.data[potision].clientSocket);
        perror_exit("write message SERVER TERMINATED  BEFORE EXECUTION");
    }
    buffer.count--;
    buffer.jobs_removed++;
    pthread_cond_signal(&buffer.cond_nonfull);
    pthread_mutex_unlock(&buffer.buffer_mutex);
    
}
//Συνάρτηση που ελέγχει αν υπάρχει job με job_id στον buffer(υπαρχει επιστρέφει την θεση αν βρεθει διαφορετικα -1)
int find_job(char * job_id){
    pthread_mutex_lock(&buffer.buffer_mutex);
    for(int i = buffer.start; i <= buffer.end;i++){
        if(strcmp(buffer.data[i].job_id,job_id) == 0 && buffer.data[i].clientSocket != -1 && buffer.data[i].job_is_removed == 0){
            pthread_mutex_unlock(&buffer.buffer_mutex);
            return i;
        }
    }
    pthread_mutex_unlock(&buffer.buffer_mutex);
    return -1;
} 
//Η λειτουργία του worked thread
void *worked_thread_routine(void *arg){
    while(1){
        buffer_value data  = obtain();
        //Εάν ο server τερματίσει το thread τερματίζει και αυτό
        if(server_terminate == 1){
            return NULL;
        }
        usleep(1000);
        //Γράφουμε στον commander αν η job έχει γίνει stop/remove
        if(write(data.clientSocket,&data.job_is_removed,sizeof(int)) <= 0){
                close(data.clientSocket);
                perror_exit("write data.job_is_removed");
        }
        //Περίπτωση που κάποιος commander έκανε stop την job πριν προλάβει να τρέξει
        if(data.job_is_removed == 1){
            buffer.jobs_removed--;
            continue;
        }
        //Υπολογίζουμε πόσα είναι τα ορίσματα της εντολής
        int number_of_args = 1;
        for(int i=0;i < strlen(data.job);i++){
            if(data.job[i] == ' '){
                number_of_args++;
            }
        }
        char * args_for_exec[number_of_args + 1]; //Για το NULL που χρειάζεται η execvp
        char * token = strtok(data.job," ");
        int count = 0;
        while (token != NULL) {
            args_for_exec[count] = token;
            token = strtok(NULL, " ");
            count++;
        }
        args_for_exec[number_of_args] = NULL;
        
        int pid = fork();
        if(pid == -1){
            perror("fork error");

        }
        else if (pid == 0){
            //child proccess
            pid_t child_pid = getpid();
            data.pid = child_pid;
            char file_name[20];
            snprintf(file_name, sizeof(file_name), "%d.output",child_pid);
            // Δημιουργία και άνοιγμα του αρχείου
            FILE *file = fopen(file_name, "w");
            if (file == NULL) {
                perror("fopen failed");
                exit(1);
            }
            //Ανακατεύθυνση της εξόδου 
            dup2(fileno(file), 1);
            // Εγγραφή στο αρχείο
            fclose(file);
            if (execvp(args_for_exec[0], args_for_exec) == -1) {
                perror("execvp failed");
                exit(EXIT_FAILURE);
            }
        }
        else {
            //parent proccess
            //Περιμένουμε να τελειώσει η διεργασία παιδί 
            int status;
            waitpid(pid,&status,0);
            pthread_mutex_lock(&processes_mutex);
            //Φτιάχνουμε το μήνυμα πριν την έξοδο της εντολής
            char * message_before = "-----";
            char * message2_before = " output start------";
            char * message_to_commander_before = malloc(strlen(message_before)+strlen(message2_before)+strlen(data.job_id)+3);
            memset(message_to_commander_before,0,strlen(message_before)+strlen(message2_before)+strlen(data.job_id)+3);
            strcat(message_to_commander_before,message_before);
            strcat(message_to_commander_before,data.job_id);
            strcat(message_to_commander_before,message2_before);
            //Γράφουμε το μέγεθος του πρώτου μηνύματος στον commander
            int size = strlen(message_to_commander_before);
            if(write(data.clientSocket,&size,sizeof(int)) <= 0){
                close(data.clientSocket);
                perror_exit("write size of message_to_commander_before");
            }
            //Γράφουμε το πρώτο μήνυμα στον commander
            if(write(data.clientSocket,message_to_commander_before,size) <= 0){
                close(data.clientSocket);
                perror_exit("write message_to_commander_before");
            }
            //Φτιάχνουμε το μήνυμα που είναι μετά την έξοδο της εντολής
            char * message_after = "-----";
            char * message2_after = " output end------";
            char * message_to_commander_after = malloc(strlen(message_after)+strlen(message2_after)+strlen(data.job_id)+3);
            memset(message_to_commander_after,0,strlen(message_after)+strlen(message2_after)+strlen(data.job_id)+3);
            strcat(message_to_commander_after,message_after);
            strcat(message_to_commander_after,data.job_id);
            strcat(message_to_commander_after,message2_after);
            size = strlen(message_to_commander_after);
            //Γράφουμε το μέγεθος του δεύτερου μηνύματος στον commander
            if(write(data.clientSocket,&size,sizeof(int)) <= 0){
                close(data.clientSocket);
                perror_exit("write size of message_to_commander_before");
            }
            //Γράφουμε το δεύτερο μήνυμα στον commander
            if(write(data.clientSocket,message_to_commander_after,size) <= 0){
                close(data.clientSocket);
                perror_exit("write message_to_commander_before");
            }
            //Φτιάχνουμε το όνομα του αρχείου που αποθηκεύτηκε η exec
            int strlen_pid = countDigits(pid);
            int size_of_file = strlen_pid + 8;
            char *outputFile = malloc(size_of_file);
            memset(outputFile,0,size_of_file);
            snprintf(outputFile, size_of_file, "%d.output", pid);
            
            //Γράφουμε το μέγεθος του ονόματος του outputfile στον commander
            if(write(data.clientSocket,&size_of_file,sizeof(int)) <= 0){
                close(data.clientSocket);
                perror_exit("write size of outputfile");
            }
            //Γράφουμε το όνομα του outputfile στον commander
            if(write(data.clientSocket,outputFile,size_of_file) <= 0){
                close(data.clientSocket);
                perror_exit("write outputfile");
            }
            
            jobs_running--;
	    usleep(10000);
            //Στέλουνμε σήμα ότι τερμάτισε η διεργασία έτσι ώστε αν έχει δοθεί η εντολή exit ο server να τερματίσει
            pthread_cond_signal(&terminate);
            pthread_mutex_unlock(&processes_mutex);
            //Αποδεσμεύουμε την μνήμη που δεσμεύσαμε, κλείνουμε το fp, διαγράφουμε το outputFile και κλείνουμε το socket
            free(message_to_commander_after);
            free(message_to_commander_before);
            free(outputFile);
            close(data.clientSocket);
        }
    }
    return NULL;
}

//Η λειτουργία του controlled thread
void *controlled_thread_routine(void * arg){
    //Βάσουμε το clientsocket και το thread_pool_size σε μια μεταβλητή και μετά κάνουμε free το arg
    ThreadArgs *args = (ThreadArgs *)arg;
    int clientsocket = args->clientSocket;
    int thread_pool_size2 = args->threadPoolSize;
    int server_socket = args->serverSocket;
    free(arg);
    int size_of_job;
    //Διαβάζουμε το μέγεθος της εντολής από τον commander 
    if((read(clientsocket,&size_of_job,sizeof(int))) <= 0){
        close(clientsocket);
        perror_exit("read size_of_job");
    }
    char *command = malloc(size_of_job+1);
    memset(command,0,size_of_job+1);
    //Διαβάζουμε την εντολή από τον commander
    if((read(clientsocket,command,size_of_job)) <= 0){
        close(clientsocket);
        perror_exit("read size_of_job");
    }
        
    //Περίπτωση που η εντολή είναι issueJob
    if(strcmp(command,"issueJob") == 0){
        //Διαβάζουμε τον αριθμό των ορισμάτων της issuejob
        int number_of_args;
        if((read(clientsocket,&number_of_args,sizeof(int))) <= 0){
            close(clientsocket);
            perror_exit("read issuejob number of args");
        }
        //Διαβάζουμε τα ορίσματα της issuejob
        char *job_elements[number_of_args];
        int total_size_of_job = 0;
        for(int i =0; i < number_of_args; i++){
            int size_of_arg;
            if((read(clientsocket,&size_of_arg,sizeof(int))) <= 0){
                close(clientsocket);
                perror_exit("read issuejob size of arg");
            }
            total_size_of_job += size_of_arg;
            job_elements[i] = malloc(size_of_arg+1);
            memset(job_elements[i],0,size_of_arg+1);
            if((read(clientsocket,job_elements[i],size_of_arg)) <= 0){
                close(clientsocket);
                perror_exit("read issuejob job_elements");
            }
        }
        //Αρχικοποιούμε το buffer value
        buffer_value value;
        value.clientSocket = clientsocket;
        int id = buffer.count +1;
        value.job_id = malloc(sizeof(6));
        memset(value.job_id,0,6);
        sprintf(value.job_id,"job_%d",id);
        value.job = malloc(total_size_of_job+1);
        memset(value.job ,0,total_size_of_job+1);
        for(int i =0; i < number_of_args; i++){
            strcat(value.job, job_elements[i]);
            if(i<number_of_args -1)
                strcat(value.job, " ");
        }
        value.job_is_removed = 0;
        value.pid = -1;
        //Βάζουμε την job στον buffer
        place(value);
        //Στέλνουμε το μήνυμα στον commander
        char  *message = "JOB <";
        char * message2 = "> SUBMITTED";
        char * message_to_commander = malloc(strlen(message)+strlen(message2)+strlen(value.job_id)+strlen(value.job)+4);
        memset(message_to_commander,0,strlen(message)+strlen(message2)+strlen(value.job_id)+strlen(value.job)+4);
        strcat(message_to_commander,message);
        strcat(message_to_commander,value.job_id);
        strcat(message_to_commander, ",");
        strcat(message_to_commander,value.job);
        strcat(message_to_commander,message2);
        int size = strlen(message_to_commander);
        //Γράφουμε το μέγεθος του μηνύματος στον commander
        if((write(clientsocket,&size,sizeof(int))) <= 0){
            close(clientsocket);
            perror_exit("write size of message");
        }
        //Γράφουμε το μήνυμα στον commander
        if((write(clientsocket,message_to_commander,size)) <= 0){
            close(clientsocket);
            perror_exit("write message");
        }
        //Αποδεσμεύουμε την μνήμη που δεσμεύσαμε 
        for(int i =0; i < number_of_args; i++){
            free(job_elements[i]);
        }
        fflush(stdout);
        //Αποδεσμεύουμε την μνήμη που δεσμεύσαμε
        free(message_to_commander);
        free(command);
    }
    //Περίπτωση που η εντολή είναι setConcurrency
    else if(strcmp(command,"setConcurrency") == 0){
        pthread_mutex_lock(&concurrency_mutex);
        int new_con;
        //Διαβάζουμε το νέο concurrency από τον commander
        if((read(clientsocket,&new_con,sizeof(int))) <= 0){
            close(clientsocket);
            perror_exit("read concurrency");
        }
        concurrency = new_con;
        char  message[100];
        sprintf(message, "CONCURRENCY SET AT %d", concurrency);
        int size = strlen(message);
        //Γράφουμε το μέγεθος του μηνύματος στον commander
        if((write(clientsocket,&size,sizeof(int))) <= 0){
            close(clientsocket);
            perror_exit("write size of message");
        }
        //Γράφουμε το μήνυμα στον commander
        if((write(clientsocket,message,size)) <= 0){
            close(clientsocket);
            perror_exit("write message");
        }
        //Ξυπνάμε τα thread έτσι ώστε να τρέξουν οι εργασίες που πρέπει αν χρειάζεται λόγω της αλλαγής του concurrency
        pthread_cond_broadcast(&buffer.cond_nonempty_or_concurrency);
        pthread_mutex_unlock(&concurrency_mutex);
        fflush(stdout);
    }
    //Περίπτωση που η εντολή είναι stop
    else if(strcmp(command,"stop") == 0){
        int size_of_job;
        //Διαβάζουμε το μέγεθος της job από τον commander
        if((read(clientsocket,&size_of_job,sizeof(int))) <= 0){
            close(clientsocket);
            perror_exit("read size of job_id");
        }
        char *job_id = malloc(size_of_job+1);
        memset(job_id,0,size_of_job+1);
        //Διαβάζουμε την job από τον commander
        if((read(clientsocket,job_id,size_of_job)) <= 0){
            close(clientsocket);
            perror_exit("read job_id");
        }
        //Εάν υπάρχει το job_id στον buffer γράφουμε job_id removed στον commander
        int found = find_job(job_id);
        if(found != -1){
            //Διαγράφουμε την job από τον buffer
            remove_job(found);
            char *message = "JOB <";
            char *message2 = "> REMOVED";
            char *message_to_commander = malloc(strlen(message)+strlen(message2)+strlen(job_id)+3);
            memset(message_to_commander,0,strlen(message)+strlen(message2)+strlen(job_id)+3);
            strcat(message_to_commander,message);
            strcat(message_to_commander,job_id);
            strcat(message_to_commander,message2);
            int size_of_message = strlen(message_to_commander);
            //Γράφουμε το μέγεθος του μηνύματος στον commander
            if(write(clientsocket,&size_of_message,sizeof(int)) <= 0){
                close(clientsocket);
                perror_exit("write size of message JOB <JOBID> removed");
            }
            //Γράφουμε το μήνυμα στον commander
            if(write(clientsocket,message_to_commander,size_of_message) <= 0){
                close(clientsocket);
                perror_exit("write message JOB <JOBID> removed");
            }
            //Αποδεσμεύουμε το message to commander
            free(message_to_commander);

        }
        //Εάν δεν υπαρχει το job_id στον buffer γράφουμε not found στον commander
        else{
            char *message = "JOB <";
            char *message2 = "> NOTFOUND";
            char *message_to_commander = malloc(strlen(message)+strlen(message2)+strlen(job_id)+3);
            memset(message_to_commander,0,strlen(message)+strlen(message2)+strlen(job_id)+3);
            strcat(message_to_commander,message);
            strcat(message_to_commander,job_id);
            strcat(message_to_commander,message2);
            int size_of_message = strlen(message_to_commander);
            //Γράφουμε το μέγεθος του μηνύματος στον commander
            if(write(clientsocket,&size_of_message,sizeof(int)) <= 0){
                close(clientsocket);
                perror_exit("write size of message JOB <JOBID> removed");
            }
            //Γράφουμε το μήνυμα στον commander
            if(write(clientsocket,message_to_commander,size_of_message) <= 0){
                close(clientsocket);
                perror_exit("write message JOB <JOBID> removed");
            }
            free(message_to_commander);
        }
        //Αποδεσμεύουμε την μνήμη που δεσμεύσαμε και κλείνουμε το clientsocket
        fflush(stdout);
        free(job_id);
        free(command);
        close(clientsocket);
    }
    //Περίπτωση που η εντολή είναι poll
    else if(strcmp(command,"poll") == 0){
        //Γράφουμε στον commander πόσα jobs έχει ο Buffer
        if(write(clientsocket,&buffer.count,sizeof(int)) <= 0){
            close(clientsocket);
            perror_exit("write size of message of jobs on buff");
        }
        //Περίπτωση που δεν έχουμε jobs στον buffer κλείνουμε το clientsocket και κάνουμε exit
        if(buffer.count == 0){
            close(clientsocket);
            pthread_exit(0);
        }

        //Περίπτωση όπου έχουμε Jobs στον buffer υπολογίζουμε το size σε char των jobs που δεν έγιναν removed από κάποια εντολή stop
        int size = 0;
        for(int i = buffer.start; i <= buffer.end;i++){
            if(buffer.data[i].job_is_removed == 0){
                size = size + strlen(buffer.data[i].job) + strlen(buffer.data[i].job_id) + 3 + 1;//3 για <,> και 1 για αλλαγή γραμμής
                
            }
        }
        //Τοποθετούμε σε μια μεταβλητή messagetocommander char * όλα τα jobs
        char * message_to_commander = malloc(size+1);
        memset(message_to_commander,0,size + 1);
        for(int i = buffer.start; i <= buffer.end;i++){
            if(buffer.data[i].job_is_removed== 0){
                strcat(message_to_commander,"<");
                strcat(message_to_commander,buffer.data[i].job_id);
                strcat(message_to_commander,",");
                strcat(message_to_commander,buffer.data[i].job);
                strcat(message_to_commander,">");
                strcat(message_to_commander,"\n");
            }
        }
        //Γράφουμε το μέγεθος του μηνύματος στον commander
        if(write(clientsocket,&size,sizeof(int)) <= 0){
            close(clientsocket);
            perror_exit("write size of message of jobs on buff");
        }
        //Γράφουμε το μήνυμα στον commander
        if(write(clientsocket,message_to_commander,size) <= 0){
            close(clientsocket);
            perror_exit("write message of jobs on buff");
        }
        //Κάνουμε free και κλείνουμε το clientsocket
        free(message_to_commander);
        fflush(stdout);
        free(command);
        close(clientsocket);
    }
    //Περίπτωση που η εντολή είναι exit
    else if(strcmp(command,"exit") == 0){
        pthread_mutex_lock(&processes_mutex);
        //Κάνουμε το server_terminate 1  ώστε να μην τρέξει καμία άλλη διεργασία 
        server_terminate = 1;
        
        
        //Ελέγχουμε αν έχουμε jobs στον buffer και αν έχουμε τα διαγράφουμε στέλοντας στους commanders μήνυμα ότι ο server τερμάτισε πριν την εκτέλεση
        if(buffer.count != 0){
            for(int i = buffer.start; i <= buffer.end; i ++){
                if(buffer.data[i].job_is_removed == 0){
                    remove_job_because_server_terminates(i);
                }
            }
        }
        //Περίμενουμε να τερματίσουν αν υπάρχουν διεργασίες που τρέχουν 
        while(jobs_running > 0){
            pthread_cond_wait(&terminate,&processes_mutex);
        }
        for (int i = 0; i < thread_pool_size2; i++) {
            pthread_cond_broadcast(&buffer.cond_nonempty_or_concurrency);
        }
        char *message = "SERVER TERMINATED";
        int size_of_message = strlen(message);
        //Γράφουμε το μέγεθος του μηνύματος στον commander
        if(write(clientsocket,&size_of_message,sizeof(int)) <= 0){
            close(clientsocket);
            perror_exit("write size of message server terminated");
        }
        //Γράφουμε το μήνυμα στον commander
        if(write(clientsocket,message,size_of_message) <= 0){
            close(clientsocket);
            perror_exit("write message server terminated");
        }
        
        //Κάνουμε destroy τα mutexes και τις cond variables, free,destory τον buffer και κλείνουμε το clientsocket
        
        pthread_mutex_unlock(&processes_mutex);
	usleep(10000);
        destroy_buffer(buffer);
        pthread_cond_destroy(&terminate);
        pthread_mutex_destroy(&concurrency_mutex);
        pthread_mutex_destroy(&processes_mutex);
        pthread_cond_destroy(&terminate);
        free(command);
        close(clientsocket);
        close(server_socket);
        fflush(stdout);
        exit(0);
    }
    //Περίπτωση που η εντολή είναι δεν είναι κάποια από τις παραπάνω κλείνουμε το socket και κάνουμε exit
    else{
        free(command);
        close(clientsocket);
    }
    pthread_exit(0);

}


int main(int argc, char* argv[]){
    //Ελέγχουμε ότι δώθηκαν σωστά τα ορίσματα
    if(argc < 4){//gia to check
        exit(1);
    }
    //Δημιουργία του ενταμιευτή
    int buffer_size = atoi(argv[2]);
    initialize(buffer_size); 
    //Αρχικοποίηση του αριθμού των worked threads
    int thread_pool_size = atoi(argv[3]);
    //Ελέγχουμε αν τα ορίσματα που δόθηκαν συμβάλουν στην ομαλή εκτέλεση του server
    if (buffer_size <= 0 || thread_pool_size <= 0) {
        fprintf(stderr, "Buffer size and thread pool size must be > 0\n");
        exit(1);
    }
    //Αρχικοποιούμε τα mutexes και την cond variable
    pthread_mutex_init (&concurrency_mutex,0);
    pthread_mutex_init (&processes_mutex,0);
    pthread_cond_init(&terminate,0); 
    //Δημιουργία των worked threads
    pthread_t worked_threads[thread_pool_size];
    for (int i = 0; i < thread_pool_size; i++) {
        pthread_create(&worked_threads[i], NULL,worked_thread_routine, NULL);
    }

    int port,serversocket;
    struct sockaddr_in server,client;
    socklen_t clientlen;
    struct sockaddr *serverptr = (struct sockaddr*)&server;
    struct sockaddr *clientptr = (struct sockaddr*)&client;
    struct hostent *rem;
    
    port = atoi(argv[1]);
    //Φτιάχνουμε ένα socket
    if((serversocket = socket(AF_INET,SOCK_STREAM,0)) < 0)
        perror_exit("socket");
    int optval = 1;
    if(setsockopt(serversocket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0)
        perror_exit("setsockopt");
    memset(&server,0,sizeof(server));
    server.sin_family = AF_INET;//internet domain
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(port); //Το port που δόθηκε

    //Bind το socket σε address
    if(bind(serversocket,serverptr,sizeof(server)) < 0){
        perror_exit("bind");
    }
    //listen για συνδέσεις
    if(listen(serversocket,thread_pool_size) < 0)
        perror_exit("listen");
    printf("listening for connections to port %d\n",port);
    while(1){
        clientlen = sizeof(client);
        //κάνουμε accept μια σύνδεση
        int clientsocket;
        if((clientsocket  = accept(serversocket,clientptr,&clientlen)) < 0){
            perror_exit("accept");
        }
        //Βρίσκουμε το όνομα του client
        if((rem = gethostbyaddr((char*)&client.sin_addr.s_addr,sizeof(client.sin_addr.s_addr),client.sin_family)) == NULL){
            herror("gethostbyaddr");
            exit(1);
        }
        
        printf("Accepted connection from %s\n",rem->h_name);
        //Παιρνάμε και το thread_pool_size και το serversocket στο controlled thread ώστε να τερματίσουμε τα thread και να κλείσουμε το socket στην περίπτωση που δωθεί η εντολή exit
        ThreadArgs *args = malloc(sizeof(ThreadArgs));
        args->clientSocket = clientsocket;
        args->threadPoolSize = thread_pool_size;
        args->serverSocket = serversocket;

        
        //Δημιουργούμε ένα controlled thread για να εκτελέσει την εντολή που μας πέρασε ο commander
        pthread_t controlled_thread;
        pthread_create(&controlled_thread,NULL,controlled_thread_routine,args);
        pthread_detach(controlled_thread);
    }
    close(serversocket);
    
    exit(0);
}
