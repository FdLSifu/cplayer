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
#include <fstream>
#include <iomanip>

void error(const char* fun, const char *msg);
void warning(const char* fun, const char *msg);

void launch_player(std::map<std::string,std::string>* playlist)
{
    std::map<std::string,std::string>::iterator it;
    std::string title("");
    FILE * fzfp = NULL;
    std::fstream ftmp;
    int pid = -1;
    std::string query;
    std::string arg;
    std::string sfzf;
    while(1)
    {

        // Create playlist file
        ftmp = std::fstream("/tmp/cplayer.tmp",std::ios_base::out);
        if (!ftmp.is_open())
            error("launch_player","fail to open /tmp/cplayer.tmp");

        for(it = playlist->begin(); it != playlist->end(); it++)
        {
            ftmp << it->first << std::endl;
        }
        ftmp.close();

        // feed fzf with the playlist
	arg = std::string("cat /tmp/cplayer.tmp | fzf --print-query -i");
        if (title[0] != 0)
        {
            // Build args
	    arg = std::string("cat /tmp/cplayer.tmp | fzf --print-query -q ");
            arg+= std::string("\"") + query + std::string("\"");
        }
        fzfp = popen(arg.c_str(),"r");
        sfzf.resize(256);
        memset(&sfzf[0],0,256);
        size_t count = fread(&sfzf[0],1,256,fzfp);
        size_t i = 0;
        while(count > 0)
        {
            i += count;
            sfzf.resize(i + 256);
            memset(&sfzf[i],0,256);
            count = fread(&sfzf[i],1,256,fzfp);
        }


        size_t pos = sfzf.find("\n");
        if (pos != -1)
        {
            query = sfzf.substr(0,pos);
            size_t pos_end = sfzf.find("\n",pos+1);
            if (pos_end != -1)
            {
                title = sfzf.substr(pos+1,pos_end-pos-1);
            }
            else
            {
                title = "";
            }
 
        }
        else
        {
            query = "";
            title = "";
        }
       // close fzf process
        pclose(fzfp);
        
        if (std::remove("/tmp/cplayer.tmp") == -1)
            error("launch_player","fail to remove /tmp/cplayer.tmp");

        // clean playlist file
        std::remove("/tmp/cplayer.tmp");

        // If parent kill previously launched mpv
        if (pid >=0)
        {
            kill(pid, SIGKILL);
            wait(NULL);
        }
       
        // if no input, we exit, (ctrl c fzf for example) 
        if ( (query.length() == 0) && (title.length() == 0) )
        {
            std::cout << "Do you want to exit [y/N]? ";
            char ans = 'N';
            std::cin>>ans;
            if ((ans == 'y') || (ans == 'Y'))
                break;
        }

        // Fork to launch a child mpv
        pid = fork();
        if (pid == -1)
        {
            error("launch_player","fail to fork");
        }
        if (pid == 0)
        {
            // Launch video player in a child
	    char *video_player = (char*)"mpv";
	    char *args[] = {video_player,(char*)(*playlist)[title].c_str(),NULL};
            
            // Silent MPV
            fclose(stdin);
            fclose(stdout);
            fclose(stderr);
            system("notify-send \"mpv launching ...\"");
            execvp(args[0],args);
            system("notify-send \"mpv error!\""); 
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
    std::stringstream sfncache;
    std::string basecache = "/tmp/cplayer.cache";
    std::size_t hash = std::hash<std::string>{}(url);
    sfncache << basecache << "_" << hash;
    std::string fncache(sfncache.str());

    std::ifstream ifs(fncache);
    if(ifs.is_open()){
        std::string content( (std::istreambuf_iterator<char>(ifs) ),
                (std::istreambuf_iterator<char>()    ) );
        *buf += content;
        return buf;
    }
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
        else
        {
            std::ofstream ofs(fncache,std::ios_base::binary);
            ofs << *buf;
            ofs.close();
        }

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
