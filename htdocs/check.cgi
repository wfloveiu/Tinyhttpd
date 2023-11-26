#!/usr/bin/perl -Tw

use strict;
use CGI;

my($cgi) = new CGI;

# 生成响应头，其中的Content-type字段是text/html
print $cgi->header('text/html');
# 还可以这样生成请求头
# print "Content-type:text/html\r\n\r\n";
print $cgi->start_html(-title => "Example CGI script",
                       -BGCOLOR => 'red');
print $cgi->h1("CGI Example");
print $cgi->p, "This is an example of CGI\n";
print $cgi->p, "Parameters given to this script:\n";
print "<UL>\n";
foreach my $param ($cgi->param)
{
 print "<LI>", "$param ", $cgi->param($param), "\n";
}
print "</UL>";
print $cgi->end_html, "\n";


# check.cgi测试文件，可以直接在浏览器中访问这个文件