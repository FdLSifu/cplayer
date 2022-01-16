#include <iostream>
#include <map>
#include <vector>
#include <cstring>
#include <mutex>
#include <curl/curl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <csignal>
#include <sys/wait.h>

void error(const char* fun, const char *msg);
void warning(const char* fun, const char *msg);

void launch_player(std::map<std::string,std::string>* playlist)
{
    std::map<std::string,std::string>::iterator it;
    std::string stitle;
    FILE * fzfp = NULL;
    FILE * ftmp = NULL;
    int pid = -1;
    char title[256] = {0};
    std::string arg;
    while(1)
    {

        // Create playlist file
        ftmp = fopen("/tmp/cplayer.tmp","w");
        if (ftmp == NULL)
            error("launch_player","fail to open /tmp/cplayer.tmp");

        for(it = playlist->begin(); it != playlist->end(); it++)
        {
            fprintf(ftmp,"%s",it->first.c_str());
            fprintf(ftmp,"\n");
        }
        fflush(ftmp);
        fclose(ftmp);

        // feed fzf with the playlist
	arg = std::string("cat /tmp/cplayer.tmp | fzf -i");
        if (title[0] != 0)
        {
	    arg = std::string("cat /tmp/cplayer.tmp | fzf -q ");
            // Remove spaces
            int l = strlen(title);
            for(int i = l-1; i > 0; i--)
            {
                if ( (title[i] < 0x20)  || (title[i] > 0x7e))
                    title[i] = 0;
                else
                    break;
            }

            // Build args
            arg+= std::string("'") + std::string(title) + std::string("'");

        }
        fzfp = popen(arg.c_str(),"r");


        // read the channel title
        memset(title,0,256);
        fgets(title,256,fzfp);

        // close fzf process
        pclose(fzfp);
        if (std::remove("/tmp/cplayer.tmp") == -1)
            error("launch_player","fail to remove /tmp/cplayer.tmp");

        if (std::string(title).size() > 1)
            stitle = std::string(title).substr(0, std::string(title).size()-1);
        else
            stitle = std::string("");
        // clean playlist file
        std::remove("/tmp/cplayer.tmp");

        // Format title
        stitle = std::string(title).substr(0, std::string(title).size()-1);

        // If parent kill previously launched mpv
        if (pid >=0)
        {
            kill(pid, SIGKILL);
            wait(NULL);
        }
       
        // if no input, we exit, (ctrl c fzf for example) 
        if (stitle.length() == 0)
           break; 

        // Fork to launch a child mpv
        pid = fork();
        if (pid == -1)
        {
            error("launch_player","fail to fork");
        }
        if (pid == 0)
        {
            // Launch video player in a child

            // Silent MPV
            fclose(stdin);
            fclose(stdout);
            fclose(stderr);

	    char *video_player = (char*)"mpv";
	    char *args[] = {video_player,(char*)(*playlist)[stitle].c_str(),NULL};
            execvp(args[0],args);
           	 
	    perror("ERROR");
            exit(0);
        }
    }

    return;
}

void *parse(std::string* raw_playlist, std::map<std::string,std::string> *playlist)
{
    std::string title;
    std::string line;
    int get_url = 0;
    int pos = 0;
    int prev = 0;
    do
    {
        pos = raw_playlist->find("\n",prev);
        line = raw_playlist->substr(prev,pos-prev);
        if (get_url)
        {
            size_t pos = line.find('\r');
            if (pos != -1)
            {
                line = line.replace(pos,1,"");
            }
            (*playlist)[title] = line;
            get_url = 0;
        }
        else if (line.find("#EXTINF") != std::string::npos)
        {
            title = "[";
            size_t pos_t = line.find("group-title=");
            size_t pos2_t = line.find('"',pos_t+13);
            title += line.substr(pos_t+13,pos2_t-pos_t-13);
            title += "] ";

            pos_t = line.find("tvg-name=");
            pos2_t = line.find('"',pos_t+10);
            title += line.substr(pos_t+10,pos2_t-pos_t-10);
            get_url = 1;
        }
        prev = pos+1;
    }
    while(pos < raw_playlist->length() && prev < raw_playlist->length());
    return NULL;
}

static size_t read_curl_cb(void *contents, size_t size, size_t nmemb, void *userp)
{
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

#define TIMETYPE curl_off_t
#define TIMEOPT CURLINFO_TOTAL_TIME_T

struct myprogress {
    TIMETYPE lastruntime; /* type depends on version, see above */ 
    CURL *curl;
};

curl_off_t last = -10;
static int xferinfo(void *p,
        curl_off_t dltotal, curl_off_t dlnow,
        curl_off_t ultotal, curl_off_t ulnow)
{
    struct myprogress *myp = (struct myprogress *)p;
    CURL *curl = myp->curl;
    TIMETYPE curtime = 0;

    curl_easy_getinfo(curl, TIMEOPT, &curtime);
    
    if  (dltotal != 0)
    {

        int perc = 100*dlnow/dltotal;
        {
            if (last == -10)
                printf("Downloading:     ");
            else if (perc > 99)
                printf("\b");
            printf("\b\b\b");
            printf("%.2d",perc);
            printf("%c",'%');
            fflush(NULL);
            last = perc;
        }
    }
    return 0;
}

std::string* get_content(char* url,std::string* buf)
{
    struct myprogress prog;
    CURLcode res;
    CURL* curl = curl_easy_init();
    std::string surl = std::string(url).substr(0, std::string(url).size()-1);
    free(url);

    if (surl.find("http") != 0)
        error("get_content","url is not valid");

    if (curl)
    {
        prog.lastruntime = 0;
        prog.curl = curl;

        curl_easy_setopt(curl, CURLOPT_URL, surl.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, read_curl_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, buf);

        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferinfo);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &prog);

        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        if (res != 0)
            error("get_content","unable to download playlist");
    }
    else
        error("get_content","unable to initialize curl");
    return buf;
}

char * readurl(char** fname)
{
    char *buf;
    FILE* fp = fopen(*fname,"r");
    if(fp == NULL)
        error("readurl","unable to open file");

    fseek(fp, 0L, SEEK_END);
    long size= ftell(fp);
    fseek(fp, 0L, SEEK_SET);
   
    buf = (char*)malloc(size);
    if (buf == NULL)
        error("readurl","cannot allocate memory");
    
    size_t s = fread(buf,sizeof(char),size,fp);
    if (s != size)
    {
        std::cout << "read " << s << " bytes over " << size << " bytes" << std::endl;
        free(buf);
        fclose(fp);
        error("readurl","cannot read the entire file");
    }

    fclose(fp);
    return buf;
}

void warning(const char * fun, const char * msg)
{
    std::cout << "Error in " << fun << ": " << msg << std::endl;
}

void error(const char * fun, const char * msg)
{
    warning(fun,msg);
    exit(-1);
}

void usage()
{
    printf("Usage: cplayer url.txt\n");
    exit(-1);
}

int main(int argc, char**argv)
{
    if (argc != 2)
        usage();

    char* fname = argv[1];
    std::cout << "Reading ..." << std::endl;
    char* url = readurl(&fname);
    
    std::cout << "Getting content ..." << std::endl;
    std::string raw_playlist;
    get_content(url,&raw_playlist);
    
    std::cout << "\nParsing ..." << std::endl;
    std::map<std::string,std::string> playlist;
    parse(&raw_playlist,&playlist);
    
    std::cout << "Launching player ..." << std::endl;
    launch_player(&playlist);
}
