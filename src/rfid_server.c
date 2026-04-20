/**
 * rfid_server.c
 *
 * HTTP server for the RFID Kit Manager GUI.
 * Listens on localhost:8765, auto-opens rfid_gui.html on startup.
 *
 * All RFID operations are performed by spawning the existing command-line
 * tools as subprocesses and parsing their text output. This avoids all
 * MercuryAPI threading issues — the server itself has no reader state.
 *
 * Endpoints:
 *   GET  /status       — port, power, temperature, connection state
 *   POST /connect      — {"port":"COM3","power":2700}
 *                        Runs rfid_kit_reader.exe to verify connection
 *   POST /disconnect   — clears stored settings
 *   POST /scan         — runs rfid_kit_reader.exe, parses tag table
 *   POST /write/part   — {"part":42}  runs rfid_tag_writer.exe --phase1
 *   POST /write/kit    — {"kit":7}    runs rfid_tag_writer.exe --phase2
 *   GET  /csv          — serves most recent CSV from output\
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <shellapi.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <direct.h>
#include <stdarg.h>
#include <inttypes.h>

#define MAX_TAGS 512

/* EPC decode helpers — same logic as rfid_common.h but standalone */
static uint16_t epc_part(const uint8_t e[12]){ return ((uint16_t)e[0]<<8)|e[1]; }
static uint16_t epc_kit (const uint8_t e[12]){ return ((uint16_t)e[2]<<8)|e[3]; }
static int epc_has_part(const uint8_t e[12]){ uint16_t v=epc_part(e); return v!=0&&v!=0xFFFF; }
static int epc_has_kit (const uint8_t e[12]){ uint16_t v=epc_kit(e);  return v!=0&&v!=0xFFFF; }

#define PORT     8765
#define BUF_SIZE 65536

/* ── Config (set by /connect) ────────────────────────────────────────────── */
static char g_port[32]   = "COM3";
static int  g_power      = 2700;
static int  g_connected  = 0;
static char g_status[256]= "Not connected";
static char g_last_csv[512] = "";

/* ── JSON helpers ────────────────────────────────────────────────────────── */
static int json_int(const char *b, const char *k, int def)
{
    const char *p=strstr(b,k); if(!p)return def;
    p=strchr(p,':'); if(!p)return def;
    while(*p==':'||*p==' ')p++;
    return atoi(p);
}
static void json_str(const char *b, const char *k, char *out, int n)
{
    const char *p=strstr(b,k); if(!p){out[0]=0;return;}
    p=strchr(p,':'); if(!p){out[0]=0;return;}
    while(*p==':'||*p==' ')p++;
    if(*p=='"'){p++;const char *e=strchr(p,'"');
        if(!e){out[0]=0;return;}
        int l=(int)(e-p);if(l>=n)l=n-1;
        memcpy(out,p,l);out[l]=0;}
    else sscanf(p,"%31s",out);
}
static void json_escape(const char *in, char *out, int n)
{
    int j=0;
    for(int i=0;in[i]&&j<n-2;i++){
        if(in[i]=='\n'){out[j++]='\\';out[j++]='n';}
        else if(in[i]=='\r'){}
        else if(in[i]=='"'){out[j++]='\\';out[j++]='"';}
        else if(in[i]=='\\'){out[j++]='\\';out[j++]='\\';}
        else out[j++]=in[i];
    }
    out[j]=0;
}

/* ── HTTP helpers ────────────────────────────────────────────────────────── */
static void send_response(SOCKET s, int code, const char *ct,
                           const char *body, int blen)
{
    char hdr[512];
    const char *cs=code==200?"OK":code==400?"Bad Request":
                   code==404?"Not Found":"Error";
    int hl=snprintf(hdr,sizeof(hdr),
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %d\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET,POST,OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Connection: close\r\n\r\n",code,cs,ct,blen);
    send(s,hdr,hl,0);
    if(body&&blen>0) send(s,body,blen,0);
}
static void send_json(SOCKET s,int code,const char *j)
{ send_response(s,code,"application/json",j,(int)strlen(j)); }
static void send_options(SOCKET s)
{
    const char *r="HTTP/1.1 204 No Content\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET,POST,OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Connection: close\r\n\r\n";
    send(s,r,(int)strlen(r),0);
}

/* ── Run subprocess, capture stdout+stderr ───────────────────────────────── */
/**
 * Runs cmd, optionally writes stdin_data to its stdin, returns
 * heap-allocated combined stdout output. Caller must free().
 * timeout_ms: max wait time (0 = wait forever).
 */
static char *run_capture(const char *cmd, const char *stdin_data,
                          DWORD timeout_ms)
{
    HANDLE hStdoutRd=NULL,hStdoutWr=NULL;
    HANDLE hStdinRd=NULL, hStdinWr=NULL;
    SECURITY_ATTRIBUTES sa={sizeof(SECURITY_ATTRIBUTES),NULL,TRUE};

    /* Stdout pipe */
    CreatePipe(&hStdoutRd,&hStdoutWr,&sa,0);
    SetHandleInformation(hStdoutRd,HANDLE_FLAG_INHERIT,0);

    /* Stdin pipe */
    CreatePipe(&hStdinRd,&hStdinWr,&sa,0);
    SetHandleInformation(hStdinWr,HANDLE_FLAG_INHERIT,0);

    STARTUPINFOA si={0}; si.cb=sizeof(si);
    si.hStdOutput=hStdoutWr;
    si.hStdError=hStdoutWr;
    si.hStdInput=hStdinRd;
    si.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;
    si.wShowWindow=SW_HIDE;

    PROCESS_INFORMATION pi={0};
    char cmdBuf[1024];
    strncpy(cmdBuf,cmd,sizeof(cmdBuf)-1);

    if(!CreateProcessA(NULL,cmdBuf,NULL,NULL,TRUE,
                       CREATE_NO_WINDOW,NULL,NULL,&si,&pi))
    {
        CloseHandle(hStdoutRd);CloseHandle(hStdoutWr);
        CloseHandle(hStdinRd); CloseHandle(hStdinWr);
        char *err=(char*)malloc(128);
        snprintf(err,128,"[ERROR] CreateProcess failed (%lu)\n",GetLastError());
        return err;
    }

    /* Close write end of stdout in parent — must do before reading */
    CloseHandle(hStdoutWr);
    /* Close read end of stdin in parent */
    CloseHandle(hStdinRd);

    /* Write stdin if provided */
    if(stdin_data && stdin_data[0]){
        DWORD written;
        WriteFile(hStdinWr,stdin_data,(DWORD)strlen(stdin_data),&written,NULL);
    }
    CloseHandle(hStdinWr);

    /* Read all stdout */
    char *out=(char*)malloc(BUF_SIZE); out[0]=0;
    int total=0;
    char tmp[4096]; DWORD nread;
    while(ReadFile(hStdoutRd,tmp,sizeof(tmp)-1,&nread,NULL)&&nread>0){
        if(total+(int)nread<BUF_SIZE-1){
            memcpy(out+total,tmp,nread);
            total+=nread; out[total]=0;
        }
    }
    CloseHandle(hStdoutRd);

    WaitForSingleObject(pi.hProcess, timeout_ms ? timeout_ms : INFINITE);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);

    return out;
}

/* ── Hex → bytes helper ──────────────────────────────────────────────────── */
static int hex2byte(char c)
{
    if(c>='0'&&c<='9') return c-'0';
    if(c>='a'&&c<='f') return c-'a'+10;
    if(c>='A'&&c<='F') return c-'A'+10;
    return 0;
}
static int hexstr_to_bytes(const char *hex, uint8_t *out, int maxlen)
{
    int n=0;
    while(hex[0]&&hex[1]&&n<maxlen){
        out[n++]=(uint8_t)(hex2byte(hex[0])<<4|hex2byte(hex[1]));
        hex+=2;
    }
    return n;
}

/* ── Parse reader output → JSON tag array ────────────────────────────────── */
/**
 * Parses lines like:
 *   22B8160E32206820C6220105   8888     5646     -45 dBm
 * and "[not programmed]" lines, builds JSON tag array string.
 */
static char *parse_scan_output(const char *output, int *count_out)
{
    char *json=(char*)malloc(MAX_TAGS*200+256);
    int pos=0, count=0;
    pos+=sprintf(json+pos,"{\"tags\":[");

    const char *line=output;
    while(*line){
        /* Find end of line */
        const char *end=strchr(line,'\n');
        if(!end) end=line+strlen(line);
        int len=(int)(end-line);

        /* Skip short lines and header lines */
        if(len>=24){
            /* Try to parse: EPC(24+) spaces Part spaces Kit spaces RSSI */
            char epc[32]={0};
            int rssi=0;
            int not_prog=0;

            /* Copy first token (EPC) */
            int i=0;
            while(i<len && line[i]!=' ' && i<31){ epc[i]=line[i]; i++; }
            epc[i]=0;

            /* Check EPC looks valid (hex chars, length 24) */
            int epc_ok=(strlen(epc)==24);
            if(epc_ok){
                for(int j=0;j<24;j++)
                    if(!((epc[j]>='0'&&epc[j]<='9')||
                         (epc[j]>='A'&&epc[j]<='F')||
                         (epc[j]>='a'&&epc[j]<='f')))
                        {epc_ok=0;break;}
            }

            if(epc_ok){
                /* Skip whitespace, check if field is blank/unset */
                while(i<len&&line[i]==' ')i++;
                if(i<len){
                    /* "---" means not programmed */
                    if(strncmp(line+i,"---",3)==0){
                        not_prog=1;
                    }
                    /* still need to advance past rssi for not_prog case */
                }

                /* Decode EPC to get programmed state */
                uint8_t ebytes[12]={0};
                int eblen=hexstr_to_bytes(epc,ebytes,12);
                int programmed=0;
                if(eblen>=4&&!not_prog)
                    programmed=epc_has_part(ebytes)&&epc_has_kit(ebytes);

                uint16_t ep= eblen>=2?epc_part(ebytes):0;
                uint16_t ek= eblen>=4?epc_kit(ebytes):0xFFFF;

                if(count>0) pos+=sprintf(json+pos,",");
                pos+=sprintf(json+pos,
                    "{\"epc\":\"%s\",\"part\":%u,\"kit\":%u,"
                    "\"rssi\":%d,\"programmed\":%s,\"bad\":false}",
                    epc, ep, ek, rssi,
                    programmed?"true":"false");
                count++;
            }
        }

        if(*end=='\0') break;
        line=end+1;
    }

    pos+=sprintf(json+pos,"],\"count\":%d}",count);
    if(count_out) *count_out=count;
    return json;
}

/* ── Parse writer output → log string ───────────────────────────────────── */
static char *parse_write_output(const char *output)
{
    /* Just return the relevant lines (OK/ERR/SKIP/WARN/BAD) */
    char *log=(char*)malloc(32768); log[0]=0;
    const char *line=output;
    while(*line){
        const char *end=strchr(line,'\n');
        if(!end) end=line+strlen(line);
        int len=(int)(end-line);
        if(len>4){
            /* Include lines with our status tags */
            if(strncmp(line,"    [ OK",8)==0||
               strncmp(line,"    [ERR",8)==0||
               strncmp(line,"    [WAR",8)==0||
               strncmp(line,"    [SKI",8)==0||
               strncmp(line,"    [BAD",8)==0||
               strncmp(line,"  - Resu",8)==0||
               strncmp(line,"  Detect",8)==0||
               strncmp(line,"  Settli",8)==0||
               strncmp(line,"  Holdin",8)==0){
                int space=32768-(int)strlen(log)-3;
                if(space>len){
                    strncat(log,line,len);
                    strcat(log,"\n");
                }
            }
        }
        if(*end=='\0') break;
        line=end+1;
    }
    return log;
}

/* ── Build tool paths ────────────────────────────────────────────────────── */
static void get_reader_cmd(char *cmd, int len){
    /* --no-csv: don't write CSV on every scan, only on explicit export */
    snprintf(cmd,len,"bin\\rfid_kit_reader.exe %s --power %d --no-csv",
             g_port,g_power);
}
static void get_writer_cmd(char *cmd, int len, const char *phase){
    snprintf(cmd,len,"bin\\rfid_tag_writer.exe %s --power %d %s",
             g_port,g_power,phase);
}

/* ── CSV path ────────────────────────────────────────────────────────────── */
static void make_csv_path(void){
    time_t now=time(NULL); struct tm *t=localtime(&now);
    _mkdir("output");
    snprintf(g_last_csv,sizeof(g_last_csv),
             "output\\rfid_kits_%04d%02d%02d_%02d%02d%02d.csv",
             t->tm_year+1900,t->tm_mon+1,t->tm_mday,
             t->tm_hour,t->tm_min,t->tm_sec);
}

/* ── Request handler ─────────────────────────────────────────────────────── */
static void handle(SOCKET c)
{
    static char buf[BUF_SIZE];
    int n=recv(c,buf,BUF_SIZE-1,0);
    if(n<=0){closesocket(c);return;}
    buf[n]=0;

    char method[8],path[128];
    sscanf(buf,"%7s %127s",method,path);
    const char *body=strstr(buf,"\r\n\r\n");
    body=body?body+4:"";

    if(!strcmp(method,"OPTIONS")){send_options(c);goto done;}

    /* GET /status */
    if(!strcmp(method,"GET")&&!strcmp(path,"/status")){
        /* Read temperature via diag tool (quick, ~500ms) */
        int temp=-1;
        if(g_connected){
            char tcmd[128];
            snprintf(tcmd,sizeof(tcmd),"bin\\rfid_diag.exe %s",g_port);
            char *tout=run_capture(tcmd,NULL,6000);
            char *tp=strstr(tout,"Temperature");
            if(tp){
                tp=strchr(tp,':');
                if(tp) temp=atoi(tp+1);
            }
            free(tout);
        }
        char j[512];
        snprintf(j,sizeof(j),
            "{\"connected\":%s,\"port\":\"%s\","
            "\"power\":%.2f,\"temperature\":%d,"
            "\"message\":\"%s\",\"tag_count\":0}",
            g_connected?"true":"false",g_port,
            g_power/100.0,temp,g_status);
        send_json(c,200,j);goto done;
    }

    /* POST /connect — validate port by doing a quick scan, store settings */
    if(!strcmp(method,"POST")&&!strcmp(path,"/connect")){
        char port[32]; int power;
        json_str(body,"\"port\"",port,sizeof(port));
        power=json_int(body,"\"power\"",2700);
        if(!port[0]){send_json(c,400,"{\"error\":\"missing port\"}");goto done;}

        /* Run a quick single-pass read to test connection.
         * Retry once — the port may still be releasing from a previous run. */
        char testcmd[256];
        snprintf(testcmd,sizeof(testcmd),
                 "bin\\rfid_kit_reader.exe %s --power %d",port,power);
        char *out=run_capture(testcmd,NULL,10000);
        int ok=strstr(out,"connected.")!=NULL||strstr(out,"Scanning")!=NULL;

        if(!ok){
            free(out);
            /* Wait 1 second for port to fully release, then retry */
            Sleep(1000);
            out=run_capture(testcmd,NULL,10000);
            ok=strstr(out,"connected.")!=NULL||strstr(out,"Scanning")!=NULL;
        }
        free(out);

        if(ok){
            strncpy(g_port,port,sizeof(g_port)-1);
            g_power=power;
            g_connected=1;
            snprintf(g_status,sizeof(g_status),
                     "Connected to %s at %.2f dBm",port,power/100.0);
        } else {
            g_connected=0;
            snprintf(g_status,sizeof(g_status),
                     "Could not connect to %s — check port and power switch",port);
        }

        char j[512];
        snprintf(j,sizeof(j),"{\"ok\":%s,\"message\":\"%s\"}",
                 ok?"true":"false",g_status);
        send_json(c,200,j);goto done;
    }

    /* POST /disconnect */
    if(!strcmp(method,"POST")&&!strcmp(path,"/disconnect")){
        g_connected=0;
        snprintf(g_status,sizeof(g_status),"Disconnected");
        send_json(c,200,"{\"ok\":true}");goto done;
    }

    /* POST /scan — run reader, parse tag table, return full output as log */
    if(!strcmp(method,"POST")&&!strcmp(path,"/scan")){
        if(!g_connected){send_json(c,400,"{\"error\":\"not connected\"}");goto done;}

        /* Brief delay so any previous process fully releases the COM port */
        Sleep(500);

        char cmd[256]; get_reader_cmd(cmd,sizeof(cmd));
        printf("Running: %s\n",cmd);

        char *out=run_capture(cmd,NULL,15000);
        printf("Output:\n%s\n---\n",out);

        int count=0;
        char *tags_json=parse_scan_output(out,&count);

        /* Escape the raw output for JSON so GUI can show it in the log */
        char *escaped_log=(char*)malloc(BUF_SIZE);
        json_escape(out,escaped_log,BUF_SIZE);
        free(out);

        snprintf(g_status,sizeof(g_status),
                 "Scan complete - %d tag(s) found",count);

        /* Build response: merge tags JSON with message and raw log */
        /* tags_json is: {"tags":[...],"count":N} */
        int tlen=(int)strlen(tags_json);
        char *resp=(char*)malloc(BUF_SIZE+tlen);
        /* Insert message and log before closing } of tags_json */
        snprintf(resp,BUF_SIZE+tlen,
                 "{\"tags\":%s,\"count\":%d,\"message\":\"%s\",\"log\":\"%s\"}",
                 /* extract just the array from tags_json */
                 "null",count,g_status,escaped_log);

        /* Actually parse it properly: tags_json = {"tags":[...],"count":N}
         * We need just the array part */
        char *arr=strstr(tags_json,"[");
        char *arr_end=strrchr(tags_json,']');
        if(arr&&arr_end){
            int alen=(int)(arr_end-arr)+1;
            char *arr_copy=(char*)malloc(alen+1);
            memcpy(arr_copy,arr,alen); arr_copy[alen]=0;
            snprintf(resp,BUF_SIZE+tlen,
                     "{\"tags\":%s,\"count\":%d,\"message\":\"%s\",\"log\":\"%s\"}",
                     arr_copy,count,g_status,escaped_log);
            free(arr_copy);
        }

        free(tags_json);
        free(escaped_log);
        send_json(c,200,resp);
        free(resp);
        goto done;
    }

    /* POST /write/part */
    if(!strcmp(method,"POST")&&!strcmp(path,"/write/part")){
        if(!g_connected){send_json(c,400,"{\"error\":\"not connected\"}");goto done;}
        int part=json_int(body,"\"part\"",-1);
        if(part<0||part>9999){send_json(c,400,"{\"error\":\"invalid part\"}");goto done;}

        Sleep(500); /* let COM port release from any previous operation */
        char cmd[256]; get_writer_cmd(cmd,sizeof(cmd),"--phase1");
        /* part number, empty line triggers scan, next/done to exit */
        char stdin_data[128];
        snprintf(stdin_data,sizeof(stdin_data),"%d\n\nnext\ndone\n",part);

        printf("Running: %s\nStdin: %s\n",cmd,stdin_data);
        char *out=run_capture(cmd,stdin_data,35000);
        printf("Output:\n%s\n---\n",out);

        char *escaped=(char*)malloc(BUF_SIZE);
        json_escape(out,escaped,BUF_SIZE);
        free(out);

        char *resp=(char*)malloc(BUF_SIZE+256);
        snprintf(resp,BUF_SIZE+256,
                 "{\"ok\":true,\"log\":\"%s\",\"message\":\"Write part complete\"}",
                 escaped);
        free(escaped);
        send_json(c,200,resp); free(resp);
        goto done;
    }

    /* POST /write/kit */
    if(!strcmp(method,"POST")&&!strcmp(path,"/write/kit")){
        if(!g_connected){send_json(c,400,"{\"error\":\"not connected\"}");goto done;}
        int kit=json_int(body,"\"kit\"",-1);
        if(kit<0||kit>9999){send_json(c,400,"{\"error\":\"invalid kit\"}");goto done;}

        Sleep(500);
        char cmd[256]; get_writer_cmd(cmd,sizeof(cmd),"--phase2");
        char stdin_data[128];
        snprintf(stdin_data,sizeof(stdin_data),"%d\n\nnext\ndone\n",kit);

        printf("Running: %s\nStdin: %s\n",cmd,stdin_data);
        char *out=run_capture(cmd,stdin_data,35000);
        printf("Output:\n%s\n---\n",out);

        char *escaped=(char*)malloc(BUF_SIZE);
        json_escape(out,escaped,BUF_SIZE);
        free(out);

        char *resp=(char*)malloc(BUF_SIZE+256);
        snprintf(resp,BUF_SIZE+256,
                 "{\"ok\":true,\"log\":\"%s\",\"message\":\"Write kit complete\"}",
                 escaped);
        free(escaped);
        send_json(c,200,resp); free(resp);
        goto done;
    }

    /* POST /csv — receive tag data from GUI, write CSV, return filename */
    if(!strcmp(method,"POST")&&!strcmp(path,"/csv")){
        /* Body: {"tags":[{"epc":"...","part":N,"kit":N,"rssi":N,"count":N},...]} */
        /* Parse tags array manually — simple enough given our format */
        _mkdir("output");
        time_t now=time(NULL); struct tm *t=localtime(&now);
        char csvpath[512];
        snprintf(csvpath,sizeof(csvpath),
                 "output\\rfid_kits_%04d%02d%02d_%02d%02d%02d.csv",
                 t->tm_year+1900,t->tm_mon+1,t->tm_mday,
                 t->tm_hour,t->tm_min,t->tm_sec);

        FILE *fp=fopen(csvpath,"w");
        if(!fp){send_json(c,500,"{\"error\":\"cannot write csv\"}");goto done;}

        fprintf(fp,"Kit Number,Part Number,EPC,RSSI (dBm),Read Count\n");

        /* Walk through body finding each tag object */
        const char *p=body;
        int rows=0;
        while((p=strstr(p,"\"epc\""))!=NULL){
            char epc[32]=""; int part=0,kit=0,rssi=0,count=0;

            /* Find epc value */
            const char *v=strchr(p,':'); if(!v){p++;continue;}
            v++; while(*v==' ')v++;
            if(*v=='"'){
                v++;
                const char *e=strchr(v,'"');
                if(e){ int l=(int)(e-v); if(l>31)l=31; memcpy(epc,v,l); epc[l]=0; }
            }

            /* Find part, kit, rssi, count in the same object (next ~200 chars) */
            char tmp[256]; int tlen=(int)strlen(p); if(tlen>255)tlen=255;
            memcpy(tmp,p,tlen); tmp[tlen]=0;

            char *pp;
            if((pp=strstr(tmp,"\"part\":"))){  part =atoi(pp+7); }
            if((pp=strstr(tmp,"\"kit\":"))){   kit  =atoi(pp+6); }
            if((pp=strstr(tmp,"\"rssi\":"))){  rssi =atoi(pp+7); }
            if((pp=strstr(tmp,"\"count\":"))){count=atoi(pp+8); }

            if(epc[0]){
                char ps[8]="---", ks[8]="---";
                if(part>0&&part!=65535) snprintf(ps,sizeof(ps),"%04d",part);
                if(kit >0&&kit !=65535) snprintf(ks,sizeof(ks),"%04d",kit);
                fprintf(fp,"\"%s\",\"%s\",\"%s\",%d,%d\n",
                        ks,ps,epc,rssi,count);
                rows++;
            }
            p++;
        }
        fclose(fp);
        strncpy(g_last_csv,csvpath,sizeof(g_last_csv)-1);

        char j[512];
        snprintf(j,sizeof(j),
                 "{\"ok\":true,\"rows\":%d,\"file\":\"%s\"}",
                 rows,csvpath);
        send_json(c,200,j);
        goto done;
    }

    /* GET /csv — serve the most recently written CSV */
    if(!strcmp(method,"GET")&&!strcmp(path,"/csv")){
        const char *csvpath=g_last_csv[0]?g_last_csv:NULL;

        /* Fallback: find latest in output\ if we don't have one cached */
        char latest[512]="";
        if(!csvpath){
            WIN32_FIND_DATAA fd;
            HANDLE hFind=FindFirstFileA("output\\rfid_kits_*.csv",&fd);
            if(hFind!=INVALID_HANDLE_VALUE){
                FILETIME best={0};
                do {
                    if(CompareFileTime(&fd.ftLastWriteTime,&best)>0){
                        best=fd.ftLastWriteTime;
                        snprintf(latest,sizeof(latest),"output\\%s",fd.cFileName);
                    }
                } while(FindNextFileA(hFind,&fd));
                FindClose(hFind);
            }
            if(latest[0]) csvpath=latest;
        }

        if(!csvpath){send_json(c,404,"{\"error\":\"no csv yet — click Export CSV first\"}");goto done;}

        FILE *fp=fopen(csvpath,"rb");
        if(!fp){send_json(c,404,"{\"error\":\"cannot open csv\"}");goto done;}
        fseek(fp,0,SEEK_END); long fsz=ftell(fp); fseek(fp,0,SEEK_SET);
        char *data=(char*)malloc(fsz+1);
        fread(data,1,fsz,fp); fclose(fp);

        char hdr[512];
        int hl=snprintf(hdr,sizeof(hdr),
            "HTTP/1.1 200 OK\r\nContent-Type: text/csv\r\n"
            "Content-Disposition: attachment; filename=\"rfid_inventory.csv\"\r\n"
            "Content-Length: %ld\r\n"
            "Access-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n",fsz);
        send(c,hdr,hl,0); send(c,data,(int)fsz,0);
        free(data);
        goto done;
    }

    /* GET /bom — read BOM.xlsx via Python helper, return parts JSON */
    if(!strcmp(method,"GET")&&!strcmp(path,"/bom")){
        /* Try files\BOM.xlsx first, then BOM.xlsx in project root */
        char bomcmd[512];
        snprintf(bomcmd,sizeof(bomcmd),
                 "python bom_export.py files\\BOM.xlsx 2>nul || "
                 "python bom_export.py BOM.xlsx 2>nul");
        char *out=run_capture(bomcmd,NULL,10000);
        if(!out||!out[0]||out[0]!='['){
            /* Try python3 */
            free(out);
            snprintf(bomcmd,sizeof(bomcmd),
                     "python3 bom_export.py files\\BOM.xlsx 2>nul || "
                     "python3 bom_export.py BOM.xlsx 2>nul");
            out=run_capture(bomcmd,NULL,10000);
        }
        if(!out||!out[0]||out[0]!='['){
            free(out);
            send_json(c,404,
                "{\"error\":\"BOM not found. Place BOM.xlsx in files\\\\ and ensure Python+pandas are installed.\"}");
            goto done;
        }
        /* out is a JSON array — wrap it */
        int olen=(int)strlen(out);
        char *resp=(char*)malloc(olen+32);
        snprintf(resp,olen+32,"{\"parts\":%s}",out);
        free(out);
        send_json(c,200,resp);
        free(resp);
        goto done;
    }

    send_json(c,404,"{\"error\":\"not found\"}");
done:
    closesocket(c);
}

static unsigned __stdcall conn_thread(void *arg)
{ handle((SOCKET)(uintptr_t)arg); return 0; }

int main(void)
{
    WSADATA wsa; WSAStartup(MAKEWORD(2,2),&wsa);

    SOCKET srv=socket(AF_INET,SOCK_STREAM,0);
    int yes=1; setsockopt(srv,SOL_SOCKET,SO_REUSEADDR,(char*)&yes,sizeof(yes));

    struct sockaddr_in addr={0};
    addr.sin_family=AF_INET;
    addr.sin_port=htons(PORT);
    addr.sin_addr.s_addr=inet_addr("127.0.0.1");

    if(bind(srv,(struct sockaddr*)&addr,sizeof(addr))!=0){
        fprintf(stderr,"ERROR: Port %d in use — is another instance running?\n",PORT);
        return 1;
    }
    listen(srv,8);

    printf("=== RFID Kit Manager Server ===\n");
    printf("Listening on http://localhost:%d\n\n",PORT);

    char gui_path[MAX_PATH];
    GetFullPathNameA("rfid_gui.html",MAX_PATH,gui_path,NULL);
    ShellExecuteA(NULL,"open",gui_path,NULL,NULL,SW_SHOWNORMAL);
    printf("Browser opened. Press Ctrl+C to stop.\n\n");

    while(1){
        SOCKET client=accept(srv,NULL,NULL);
        if(client==INVALID_SOCKET) continue;
        /* Each connection gets its own thread — but operations are
         * sequential within the subprocess, so no MercuryAPI conflicts */
        _beginthreadex(NULL,0,conn_thread,(void*)(uintptr_t)client,0,NULL);
    }

    WSACleanup();
    return 0;
}
