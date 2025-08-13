#include <yaml.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <yaml_file>\n", argv[0]);
        return 1;
    }
    
    FILE* file = fopen(argv[1], "r");
    if (!file) {
        perror("fopen");
        return 1;
    }
    
    yaml_parser_t parser;
    yaml_event_t event;
    
    if (!yaml_parser_initialize(&parser)) {
        fprintf(stderr, "Failed to initialize parser\n");
        fclose(file);
        return 1;
    }
    
    yaml_parser_set_input_file(&parser, file);
    
    printf("=== YAML Event Debug for %s ===\n", argv[1]);
    
    int event_count = 0;
    do {
        if (!yaml_parser_parse(&parser, &event)) {
            fprintf(stderr, "Parse error: %s\n", parser.problem);
            if (parser.problem_mark.line || parser.problem_mark.column) {
                fprintf(stderr, "Error at line %zu, column %zu\n", 
                       parser.problem_mark.line + 1, parser.problem_mark.column + 1);
            }
            break;
        }
        
        const char* event_names[] = {
            "NONE", "STREAM_START", "STREAM_END", "DOCUMENT_START", "DOCUMENT_END",
            "ALIAS", "SCALAR", "SEQUENCE_START", "SEQUENCE_END", "MAPPING_START", "MAPPING_END"
        };
        
        printf("Event %d: %s", event_count, 
               (event.type < sizeof(event_names)/sizeof(event_names[0])) 
               ? event_names[event.type] : "UNKNOWN");
        
        if (event.type == YAML_SCALAR_EVENT) {
            printf(" = '%s'", (char*)event.data.scalar.value);
        }
        
        printf("\n");
        
        yaml_event_delete(&event);
        event_count++;
        
        // Safety limit
        if (event_count > 1000) {
            printf("Stopping at 1000 events (safety limit)\n");
            break;
        }
        
    } while (event.type != YAML_STREAM_END_EVENT);
    
    yaml_parser_delete(&parser);
    fclose(file);
    
    printf("=== Total events: %d ===\n", event_count);
    return 0;
}