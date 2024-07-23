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
//Συνάρτηση εκτύπωσης μηνμύματος και εξόδου του προγράμματος σε περίπτωση λάθους 
void perror_exit(char *message){
    perror(message);
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]){
    int port,sock;
    struct sockaddr_in server;
    struct sockaddr *serverptr = (struct sockaddr*)&server;
    struct hostent* rem;
    //Ελέγχουμε αν τα ορίσματα δόθηκαν σωστά
    if(argc < 4 ){
        printf("Please give host name,port number and command\n");
        exit(1);
    }
    //Δημιουργούμε ένα socket
    if((sock = socket(AF_INET,SOCK_STREAM,0)) < 0){
        perror_exit("socket");
    }
    //Βρίσκουμε την διεύθυνση του server
    if((rem = gethostbyname(argv[1])) == NULL){
        herror("gethostbyname");
        exit(1);
    }
    port = atoi(argv[2]);//port number από string σε Int
    server.sin_family = AF_INET;//internet domain
    memcpy(&server.sin_addr,rem->h_addr,rem->h_length);
    server.sin_port = htons(port); //server port από host σε network
    //Έναρξη σύνδεσης
    if(connect(sock,serverptr,sizeof(server)) < 0)
        perror_exit("connect");
    printf("Connecting to %s port %d\n",argv[1],port);
    //Γράφουμε το size της εντόλης στον server
    int size_of_job = strlen(argv[3]);
    if(write(sock,&size_of_job,sizeof(int)) <= 0){
        close(sock);
        perror_exit("write size of argv[3]");
    }
    //Γράφουμε την εντολή στον server
    if(write(sock,argv[3],size_of_job) <= 0){
        close(sock);
        perror_exit("write size of argv[3]");
    }


    //Περίπτωση που η εντολή είναι issueJob
    if(strcmp(argv[3],"issueJob") == 0){
        int number_of_args = argc -4 ;
        //Γράφουμε τον αριθμό των ορισμάτων της εντολής στον server
        if((write(sock,&number_of_args,sizeof(int))) <= 0){
            close(sock);
            perror_exit("write issueJob number of args");
        }
        //Γράφουμε τα ορίσματα της εντολής στον server
        for(int i = 0; i < argc - 4; i++){
            int size_of_arg = strlen(argv[i+4]);
            if((write(sock,&size_of_arg,sizeof(int))) <= 0){
                close(sock);
                perror_exit("write issueJob size of arg");
            }
            if((write(sock,argv[i+4],size_of_arg)) <= 0){
                close(sock);
                perror_exit("write issueJob arg");
            }

        }
        //Διαβάζουμε το μέγεθος του μηνύματος που θα στείλει ο server 
        int size_of_message;
        if((read(sock,&size_of_message,sizeof(int))) <= 0){
            close(sock);
            perror_exit("read size of message  'job_xx SUBMITTED' ");
        }
        char * message = malloc(size_of_message+1);
        memset(message,0,size_of_message+1);
        //Διαβάζουμε το μήνυμα από τον server
        if((read(sock,message,size_of_message)) <= 0){
            close(sock);
            perror_exit("read message 'job_xx SUBMITTED'");
        }
        //Εκπτυπώνουμε το μήνυμα
        printf("%s\n",message);
        //Ελέγχουμε αν η εντολή έχει διαγράφει από κάποιον άλλον commander πριν ο server την εκτελέσει
        int job_is_removed;
        if((read(sock,&job_is_removed,sizeof(int))) <= 0){
            close(sock);
            perror_exit("read job_is_removed");
        }
        //Αν η εντολή έχει διαγραφεί τυπώνουμε αντίστοιχο μήνυμα και ο commander τερματίζει
        if(job_is_removed == 1){
            printf("A client remove the job before executes\n");
            close(sock);
            exit(0);
        }
        //Διαβάζουμε το μέγεθος του message before από τον server
        int size_of_message_before;
        if((read(sock,&size_of_message_before,sizeof(int))) <= 0){
            close(sock);
            perror_exit("read size_of_message_before");
        }
        char * message_before = malloc(size_of_message_before+1);
        memset(message_before,0,size_of_message_before+1);
        //Διαβάζουμε το message before από τον server(JOB <job_n,job> SUBMITTED)
        if((read(sock,message_before,size_of_message_before)) <= 0){
            close(sock);
            perror_exit("read message_before");
        }
        //Περίπτωση που ο server τερμάτισε πριν τρέξει την εντολή
        if(strcmp(message_before,"SERVER TERMINATED BEFORE EXECUTION") == 0){
            printf("%s\n",message_before);
            close(sock);
            free(message);
            free(message_before);
            exit(0);
        }
        //Εκτυπώνουμε το message before
        printf("%s",message_before);
        
        
        //Διαβάζουμε το μέγεθος του message after
        int size_of_message_after;
        if((read(sock,&size_of_message_after,sizeof(int))) <= 0){
            close(sock);
            perror_exit("read size_of_message_after");
        }
        char * message_after = malloc(size_of_message_after+1);
        memset(message_after,0,size_of_message_after+1);
        //Διαβάζουμε το message after(---job_n output end------)
        if((read(sock,message_after,size_of_message_after)) <= 0){
            close(sock);
            perror_exit("read message_after");
        }
        printf("\n");
        //Διαβάζουμε το μέγεθος του ονόματος του outputfile
        int size_of_outfile;
        if((read(sock,&size_of_outfile,sizeof(int))) <= 0){
            close(sock);
            perror_exit("read size_of_outfile");
        }
        char *outputFile = malloc(size_of_outfile+1);
        memset(outputFile,0,size_of_outfile+1);
        //Διαβάζουμε το message after(---job_n output end------)
        if((read(sock,outputFile,size_of_outfile)) <= 0){
            close(sock);
            perror_exit("read outputfile name");
        }
        //Ανοίγουμε το αρχείο διαβάζουμε και εκτυπώνουμε τα περιεχόμενά του
        FILE *fp = fopen(outputFile, "r");
        char message_of_file[1024];
        int i;
        while (fgets(message_of_file, 1024, fp) != NULL) {
            printf("%s",message_of_file);
        }
        

        //Τυπώνουμε το message after
        printf("%s\n",message_after);
        fflush(stdout);
        //Αποδεσμεύουμε την μνήμη που δεσμεύσαμε, κλείνουμε το socket και κάνουμε exit
        fclose(fp);
        remove(outputFile);
        free(outputFile);
        free(message_after);
        free(message_before);
        free(message);
        close(sock);
        exit(0);
    }
    //Περίπτωση που η εντολή είναι setConcurrency
    else if(strcmp(argv[3],"setConcurrency") == 0){
        int new_con = atoi(argv[4]);
        // Γράφουμε το νέο concurrency στον server
        if(write(sock,&new_con,sizeof(int)) <= 0){
            close(sock);
            perror_exit("write concurrency");
        }
        //Διαβάζουμε μέγεθος του μηνύματος 
        int size_of_message;
        if((read(sock,&size_of_message,sizeof(int))) <= 0){
            close(sock);
            perror_exit("read message");
        }
        char * message = malloc(size_of_message+1);
        memset(message,0,size_of_message+1);
        //Διαβάζουμε το μήνυμα 
        if((read(sock,message,size_of_message)) <= 0){
            close(sock);
            perror_exit("read message");
        }
        //Εκτυπώνουμε το μήνυμα
        printf("%s\n",message);
        fflush(stdout);
        //Αποδεσμεύουμε την μνήμη που δεσμεύσαμε, κλείνουμε το socket και κάνουμε exit
        free(message);
        close(sock);
        exit(0);
    }
    //Περίπτωση που η εντολή είναι stop
    else if(strcmp(argv[3],"stop") == 0){
        int size = strlen(argv[4]);
        //Γράφουμε το size του argv[4] στον server
        if(write(sock,&size,sizeof(int)) <= 0){
            close(sock);
            perror_exit("write strlen argv[3]");
        }
        //Γράφουμε το argv[4] στον server
        if(write(sock,argv[4],size) <= 0){
            close(sock);
            perror_exit("write argv[3]");
        }
        //Διαβάζουμε το μέγεθος του μηνύματος που επιστρέφει ο server
        int size_of_message;
        if(read(sock,&size_of_message,sizeof(int)) <= 0){
            close(sock);
            perror_exit("read size of message");
        }
    
        char * message = malloc(size_of_message+1);
        memset(message,0,size_of_message+1);
        //Διαβάζουμε το μήνυμα από τον server
        if(read(sock,message,size_of_message) <= 0){
            close(sock);
            perror_exit("read message");
        }
        //Εκτυπώνουμε το μήνυμα 
        printf("%s\n",message);
        fflush(stdout);
        //Αποδεσμεύουμε την μνήμη που δεσμεύσαμε, κλείνουμε το socket και κάνουμε exit
        free(message);
        close(sock);
        exit(0);
    }
    //Περίπτωση που η εντολή είναι poll
    else if(strcmp(argv[3],"poll") == 0){
        int count;
        //Διαβάζουμε το μέγεθος του buffer από τον server
        if(read(sock,&count,sizeof(int)) <= 0){
            close(sock);
            perror_exit("read size of message");
        }
        //Αν ο buffer είναι άδειος εκτυπώνουμε αντίστοιχο μήνυμα
        if(count == 0){
            printf("Empty buffer\n");
            close(sock);
            exit(0);
        }
        //Διαβάζουμε το μέγεθος του μηνύματος που θα στείλει ο server
        int size_of_message;
        if(read(sock,&size_of_message,sizeof(int)) <= 0){
            close(sock);
            perror_exit("read size of message");
        }
        
        char * message = malloc(size_of_message+1);
        memset(message,0,size_of_message+1);
        //Διαβάζουμε το μήνυμα από τον server
        if(read(sock,message,size_of_message) <= 0){
            close(sock);
            perror_exit("read message");
        }
        //Εκτυπώνουμε το μήνυμα
        printf("%s",message);
        //Αποδεσμεύουμε την μνήμη που δεσμεύσαμε, κλείνουμε το socket και κάνουμε exit
        free(message);
        close(sock);
        exit(0);
    }
    //Περίπτωση που η εντολή είναι exit
    else if(strcmp(argv[3],"exit") == 0){
        int size_of_message;
        //Διαβάζουμε το μέγεθος του μηνύματος από τον server
        if(read(sock,&size_of_message,sizeof(int)) <= 0){
            close(sock);
            perror_exit("read size of message");
        }
        char * message = malloc(size_of_message+1);
        memset(message,0,size_of_message +1);
        //Διαβάζουμε το μήνυμα από τον server
        if(read(sock,message,size_of_message) <= 0){
            close(sock);
            perror_exit("read  message");
        }
        //Εκτυπώνουμε το μήνυμα 
        printf("%s\n",message);
        //Αποδεσμεύουμε την μνήμη που δεσμεύσαμε, κλείνουμε το socket και κάνουμε exit
        free(message);
        close(sock);;
        exit(0);
    }
    //Περίπτωση που η εντολή είναι δεν είναι κάποια από τις παραπάνω κλείνουμε το socket και κάνουμε exit
    else{
        close(sock);
        exit(0);
    }
}
