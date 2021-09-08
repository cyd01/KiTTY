typedef struct psocks_state psocks_state;

typedef struct PsocksPlatform PsocksPlatform;
typedef struct PsocksDataSink PsocksDataSink;

/* indices into PsocksDataSink arrays */
typedef enum PsocksDirection { UP, DN } PsocksDirection;

typedef struct PsocksDataSink {
    void (*free)(PsocksDataSink *);
    BinarySink *s[2];
} PsocksDataSink;
static inline void pds_free(PsocksDataSink *pds)
{ pds->free(pds); }

PsocksDataSink *pds_stdio(FILE *fp[2]);

struct PsocksPlatform {
    PsocksDataSink *(*open_pipes)(
        const char *cmd, const char *const *direction_args,
        const char *index_arg, char **err);
    void (*start_subcommand)(strbuf *args);
};

psocks_state *psocks_new(const PsocksPlatform *);
void psocks_free(psocks_state *ps);
void psocks_cmdline(psocks_state *ps, int argc, char **argv);
void psocks_start(psocks_state *ps);
