#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


/* Specifics */
// all the numbers defined bellow are in bytes
#define PAGE_SIZE 256 // page size
#define FRAME_SIZE 256 // frame size
#define PHY_MEM_SIZE 128 * FRAME_SIZE // memory size = number of frames * frame size (32,768)
#define PAGE_TABLE_SIZE 256 // number of entries in page table
#define TLB_SIZE 16 // number of entries in TLB


/* LRU policy for the TLB and also page table replacement, this fuction returns the index of the TLB or page table to be replaced next time based on a history array, to determine which page has been used the least */
/* we store a history of the addresses when each page has been used in the TLB or page table, we use the history array in this function */
int LRU_index(int hist[], int size, int min){
    
    int index = 0;
    
    if (size == TLB_SIZE){ // this is for TLB (smaller)
        for(int i = 0; i < size; i++){ // search the hist array to see if there exists any other element less than min
            if(hist[i] < min){
                min = hist[i]; // then replace it
                index = i;
            } // at the end find the min of the hist array
        }
    }
    else { // this is for page table
        for(int i = 0; i < size; i++){ // search the hist array to see if there exists any other element less than min
            if(hist[i] < min && hist[i] != 0){ // because this bit in page table is both the history and valid bit , so if it is not valid do not consider it as minumum
                min = hist[i]; // then replace it
                index = i;
            } // at the end find the min of the hist array
        }
    }

    return index;
}


int main(int argc, char *argv[]) {
    
    char physical_memory[PHY_MEM_SIZE]; // physical memory
    int dirty_bit[PHY_MEM_SIZE] = {}; // dirty bit array coresponding to each frame of the memory
    int page_table[PAGE_TABLE_SIZE]; // page table
    int TLB[TLB_SIZE][2]; // translation look aside buffer (TLB)
    char temp[PAGE_SIZE]; // the back store is kept in this array
    char* filename_in = argv[1];; // input file name specifying addresses
    char* filename_out = "results.txt"; // output file
    char line[16]; // each line in input file , we have R or W as well in this set
    
    int virtual_address, physical_address, page_number, frame_number, offset;
    char sign_byte_value;
    int mask = 255; // equal to 0000000011111111 , later use this mask to obtain offset
    int mem_full = 0;
    int TLB_index = 0; // this index is used for LRU replacement selection for next time
    int TLB_hit_counter = 0;
    int page_fault_counter = 0;
    int address_counter = 0;
    int memory_index = 0;
    int TLB_history[TLB_SIZE] = {}; // this history array is used in LRU policy
    int page_table_existance[PAGE_TABLE_SIZE] = {}; // this array helps to know whether a frame exists in the page_table or not , valid bit for the page table entries, first all zero meaning no page is valid; and also this array keeps track of the history of the addresses each page has been used (using this history implementing LRU page replacment policy)
    int read_write; // this determines whether the address is for read (0) or write (1), if write so it will get dirty later
    int memory_write_counter = 0; // the number od time we write back to swap
    
    
    /* First checking the files: input file, output file, and the backing store file, other output file */
    if (filename_in == NULL) { // user did not write the name of the input file in the command line, or file does not exists
        // Usage: ./m2 addresses.txt
        printf("Please enter input file as addresses. For example type:\n");
        printf("./m3 addresses2.txt\n");
        exit(1);
        
    } else { // the user inputs the name of the input file that contains addresses
        
        // the input file containing the addresses to translate
        FILE* infile = fopen(filename_in, "r");
        if ( infile == NULL) {
            // error and exit
            printf("Input file could not be opened.\n");
            printf("Please check if the file exists.\n");
            exit(1);
        }
        
        // the output file to write the results
        FILE* outfile = fopen(filename_out, "w");
        if ( outfile == NULL) {
            // error and exit
            printf("Output file could not be opened.\n");
            exit(1);
        }
        
        // opening the back store file to get pages from when page fault happens
        FILE* store_file = fopen("BACKING_STORE.bin", "rb"); // Open for reading only
        if ( store_file == NULL) {
            // error and exit
            printf("BACKING_STORE.bin file could not be opened.\n");
            printf("Please check if the file exists.\n");
            exit(1);
        }
        
        // the output file to write the sign byte values
        FILE* values_file = fopen("values_Set3.txt", "w");
        if ( values_file == NULL) {
            // error and exit
            printf("Output file could not be opened.\n");
            exit(1);
        }
        
        // the output file to write the statistics of memory writes
        FILE* memory_write_file = fopen("memory_frames_writes_statistics.txt", "w");
        if ( memory_write_file == NULL) {
            // error and exit
            printf("Output file could not be opened.\n");
            exit(1);
        }
        
        /* Now translating all the logical addresses into physical addresses */
        while (fgets(line, sizeof(line), infile)) { // read addresses line by line
            // each address has to be converted into numbers
            
            for (int ii = 0; ii< sizeof(line); ii++){ // search for the R or W character in each line
                if (line[ii] == 'R'){ // if it is R, so read_write will be set to 0 meaning Read
                    read_write = 0;
                    line[ii] = '\0'; // replace R with empty space so that each line will just contain the virtual address
                }
                if (line[ii] == 'W'){ // if it is W, so read_write will be set to 1 meaning Write
                    read_write = 1;
                    line[ii] = '\0'; // replace W with empty space so that each line will just contain the virtual address
                }
            }

            virtual_address = atoi(line);
            address_counter++; // one addresses had been read so counter increases
            
            /* calculating the page number from the logical address */
            page_number = virtual_address >> 8; // get 8 left-most bits

            /* calculating the offset from the logical address. */
            offset = virtual_address & mask; // get 8 right-most bits
            
            
            /*  First => TLB */
            /* using page number we search for the frame number in TLB */
            int TLB_found = 0;
            for (int i = 0; i< TLB_SIZE; i++) {
                if (TLB[i][0] == page_number){ // the page is in TLB
                    TLB_history[TLB_index] = address_counter; // keeping track of the history of the address each page is used in TLB (the least used one will be picked later when we update the TLB)
                    TLB_hit_counter++;
                    frame_number = TLB[i][1]; // this is the frame number we will look in memory
                    TLB_found = 1;
                    break;
                }
            }
            
            if (TLB_found) { // if the page number was found in the TLB, so TLB hit and calculate the physical address
                
                page_table_existance[page_number] = address_counter; // the last time we use this page is the current address counter
                
                physical_address = frame_number + offset; // calculating the physical address
                sign_byte_value = physical_memory[physical_address]; // get the value in the physical address of the memory

            } else { // if the page number was not found in the TLB, so TLB miss
                
                /* Second => Page Table */
                /* searching for the page number in the page table */
                
                if (page_table_existance[page_number] != 0) { // if the page number's frame is found in the page table, meaning that the page is valid
                    // it was found in the page table, so get the correct frame number from it
                    frame_number = page_table[page_number];
                    
                    page_table_existance[page_number] = address_counter;
                    
                    physical_address = frame_number + offset; // calculating the physical address
                    sign_byte_value = physical_memory[physical_address]; // get the value in the physical address of the memory
                    
                    /* Now updating the TLB with the page number and the page frame we got */
                    TLB[TLB_index][0] = page_number;
                    TLB[TLB_index][1] = frame_number;
                    TLB_history[TLB_index] = address_counter; // keeping track of the history of the address each page is used in TLB (the least used one will be picked later when we update the TLB)
                    TLB_index = LRU_index(TLB_history,TLB_SIZE,address_counter); // getting the next TLB index to be replaced using LRU policy
                    
         
                    
                } else { // if the page number is not found in the page table, not valid, so page fault

                    page_fault_counter++; // page fault happens so increase page fault counter

                    
                    if (mem_full == 0) { // memory is not full and there exists an empty frame
                        
                        /* because of the page fault,
                         we read a page from the BACKING_STORE.bin file
                         and store it in an empty frame in the physical memory. */
                        fseek(store_file,page_number*PAGE_SIZE,SEEK_SET); // seeking the page number
                        fread(temp,1,PAGE_SIZE,store_file); // read it into temp array
                        memcpy(physical_memory + memory_index, temp, PAGE_SIZE); // copy temp into physical memory at the memory_index frame
                        
                        frame_number = memory_index; // the frame number in the memory is the index of the memory
                        physical_address = frame_number + offset; // calculating the physical address
                        sign_byte_value = physical_memory[physical_address]; // get the value in the physical address of the memory
                        
                        /* Updating page table with the frame number we got */
                        page_table[page_number] = frame_number;
                        page_table_existance[page_number] = address_counter; // now that we bring the frame from the backing store, the frame exists in the page table with the time set to the current address counter (next time just return the frame for the requested page number and no need to search the back store anymore)
                        
                        /* setting dirty bit for that frame */
                        if (read_write == 1){ // if it was set to Write
                            dirty_bit[frame_number] = 1; // set dirty bit to 1 for that frame of the memory
                        }
                        
                        /* Updating TLB also, with the page number and the corresponding frame number */
                        TLB[TLB_index][0] = page_number;
                        TLB[TLB_index][1] = frame_number;
                        TLB_history[TLB_index] = address_counter; // keeping track of the history of the address each page is used in TLB (the least used one will be picked later when we update the TLB)
                        TLB_index = LRU_index(TLB_history,TLB_SIZE,address_counter); // getting the next TLB index to be replaced using LRU policy
                        
                        
                        if (memory_index < PHY_MEM_SIZE - FRAME_SIZE) { // if we do not reach the end of memory size
                            memory_index += FRAME_SIZE; // index is increased by the frame size
                            
                        }
                        else { // now we have reached to the end of the memory size
                            
                            mem_full = 1; // memory is full
                        }
                        
                    } else { // memory is full and there is no empty frame in it
                        /* using LRU Policy for page replacement */
                        /* in this policy we assume the address counter of every page is the history of the time we used that exact page so we pick the least recently used one to swap */
                        
                        int page_replace = LRU_index(page_table_existance,PAGE_SIZE,address_counter); // the selected page to be replaced (the least recent used one)

                        int selected_frame_number = page_table[page_replace]; // this frame is now free in physical memory and will be replaced with frame we get from the backing store
                        
                        /* now checking the dirty bit for the selected frame */
                        if (dirty_bit[selected_frame_number] == 1 ){ // checking the dirty bit, if it is dirty we will write the page back to the backing store
                            fseek(store_file,page_replace*PAGE_SIZE,SEEK_SET); // seeking the replacing page number
                            fwrite(physical_memory + (page_replace*PAGE_SIZE), 1, PAGE_SIZE, store_file); // writing back
                            memory_write_counter++;
                            fprintf(memory_write_file,"%d\n",selected_frame_number); // writing the frame in statistics file

                        }
                        
                        page_table_existance[page_replace] = 0; // it is not valid anymore because the corresponding frame has been replaced in memory

                        
                        /* because of the page fault,
                         we read a page from the BACKING_STORE.bin file
                         and store it in the selected frame (LRU) in the physical memory. */
                        fseek(store_file,page_number*PAGE_SIZE,SEEK_SET); // seeking the page number
                        fread(temp,1,PAGE_SIZE,store_file); // read it into temp array
                        memcpy(physical_memory + selected_frame_number, temp, PAGE_SIZE); // copy temp into physical memory at the selected frame corresponding to replacing page we picked using LRU policy
                        
                        physical_address = selected_frame_number + offset; // calculating the physical address
                        sign_byte_value = physical_memory[physical_address]; // get the value in the physical address of the memory
                        
                         /* Updating page table entry with the new frame number we got */
                        page_table[page_number] = selected_frame_number;
                        page_table_existance[page_number] = address_counter; // now that we bring the frame from the backing store, the frame exists in the page table with the time set to the current address counter (next time just return the frame for the requested page number and no need to search the back store anymore)
                        
                        /* setting dirty bit for the new frame in memory */
                        if (read_write == 1){ // if it was set to Write, set dirty bit to 1
                            dirty_bit[selected_frame_number] = 1;
                        }
                        else {
                            dirty_bit[selected_frame_number] = 0; // else it is not dirty, because it is read only
                        }

                        /* Updating TLB also, with the page number and the corresponding frame number */
                        TLB[TLB_index][0] = page_number;
                        TLB[TLB_index][1] = selected_frame_number;
                        TLB_history[TLB_index] = address_counter; // keeping track of the history of the address each page is used in TLB (the least used one will be picked later when we update the TLB)
                        TLB_index = LRU_index(TLB_history,TLB_SIZE,address_counter); // getting the next TLB index to be replaced
                        
                    }
                }
            }
            
            /* Now printing the results as virtual address, physical address and the sign byte value for the corresponding address in physical memory */
            fprintf(outfile, "Virtual address: %d Physical address: %d Value: %d\n", virtual_address,physical_address,sign_byte_value);
            fprintf(values_file, "%d\n", sign_byte_value); // keeping just values in a file to compare to Set1 and Set2 values
        } // end of input file, all the virtual addresses have been translated
        
        // closing all the files
        fclose(infile);
        fclose(outfile);
        fclose(store_file);
        fclose(values_file);
        fclose(memory_write_file);
        
        /* Now calculating rates and statistics at the end */
        float page_fault_rate = (float) page_fault_counter / (float) address_counter;
        float TLB_rate = (float) TLB_hit_counter / (float) address_counter;
        float memory_write_rate = (float) memory_write_counter / (float) address_counter;
        
        /* Printing the statistics */
        printf("Statistics of Set 3 \n");
        printf("    TLB Hits :\n");
        printf("    # of TLB Hits : %d\n", TLB_hit_counter);
        printf("    TLB Hit Rate : %.4f%%\n", TLB_rate*100);
        printf("    Page Faults :\n");
        printf("    # of Page Faults : %d\n", page_fault_counter);
        printf("    Page Fault Rate : %.4f%%\n", page_fault_rate*100);
        printf("    # of Memory Writes : %d\n", memory_write_counter);
        printf("    Memory Write Rate : %.4f%%\n", memory_write_rate*100);
        printf("    Total # of Addresses : %d\n", address_counter);
       
    }
    
    return 0;
}